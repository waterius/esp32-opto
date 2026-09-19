#include "net.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <ping/ping_sock.h>

#include <atomic>

#include "../core/link_guard.h"
#include "../core/wifi_policy.h"
#include "log.h"

namespace net {
namespace {

const uint32_t HTTP_TIMEOUT_MS = 12000;  // как SERVER_TIMEOUT в waterius
const uint32_t RADIO_OFF_MS = 200;       // пауза между выключением и включением радио
const IPAddress FALLBACK_DNS(8, 8, 8, 8);  // как DEF_FALLBACK_DNS в waterius

core::WifiPolicy policy;
core::LinkGuard link;
bool linkDeadLogged = false;
bool probeImpossibleLogged = false;

// Ответ на пинг приходит в задачу ping — отсюда только флаг.
std::atomic<bool> pingReplied{false};

const uint32_t PING_TIMEOUT_MS = 1500;

WiFiClientSecure tls;
char apName_[24] = "";
bool ap_ = false;
uint8_t apChannel_ = 0;
bool wasConnected = false;
bool fastConnectFresh = false;
bool rebootWanted = false;
bool hasSsid_ = false;
bool everConnected = false;  // с текущими настройками сети хоть раз подключились
bool safeMode_ = false;
std::atomic<uint32_t> portalFedMs_{0};  // когда человек последний раз трогал страницы
char error_[64] = "";        // причина отказа для страницы /wifi (кириллица — 2 байта на букву)

// Пишет колбэк событий SDK (задача event loop), читает loop().
std::atomic<bool> gotIp_{false};
std::atomic<uint16_t> lastReason_{0};
std::atomic<uint32_t> disconnects_{0};
// Что думает драйвер. Внутренний взгляд: верить ему на слово нельзя —
// WiFi.status() умеет застревать в WL_CONNECTED на мёртвом соединении.
bool driverConnected() { return gotIp_.load() && WiFi.status() == WL_CONNECTED; }

bool hasBssid(const uint8_t bssid[6]) {
    for (int i = 0; i < 6; ++i)
        if (bssid[i]) return true;
    return false;
}

// Коды из esp_wifi_types.h. В логе без них не понять, почему рвётся связь:
// «не найдена» и «не принят пароль» лечатся совершенно по-разному.
const char* reasonName(uint16_t reason) {
    switch (reason) {
        case 1: return "не указана";
        case 2: return "истёк срок аутентификации";
        case 4: return "роутер не дождался активности";
        case 8: return "роутер разорвал ассоциацию";
        case 15: return "рукопожатие не завершилось — неверный пароль или слабый сигнал";
        case 200: return "слабый сигнал";
        case 201: return "сеть не найдена";
        case 202: return "аутентификация отклонена";
        case 203: return "ассоциация отклонена";
        case 204: return "рукопожатие не уложилось в срок";
        default: return "см. esp_wifi_types.h";
    }
}

// Роутер ответил отказом — ждать больше нечего. Спрашивать WiFi.status()
// бесполезно: на первой попытке arduino-esp32 не выставляет WL_CONNECT_FAILED
// даже при AUTH_FAIL (WiFiGeneric.cpp, условие !first_connect), а молча уходит
// в переподключение — отказ по паролю не отличить от молчания.
bool refused(uint16_t reason) {
    return reason == WIFI_REASON_AUTH_EXPIRE || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
           reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_HANDSHAKE_TIMEOUT ||
           reason == WIFI_REASON_NO_AP_FOUND;
}

void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            gotIp_.store(true);
            break;
        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
        case ARDUINO_EVENT_WIFI_STA_STOP:
            gotIp_.store(false);
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
            gotIp_.store(false);
            uint16_t reason = info.wifi_sta_disconnected.reason;
            lastReason_.store(reason);
            disconnects_.fetch_add(1);
            Log.warn("Wi-Fi: разрыв, причина %u — %s\n", reason, reasonName(reason));
            break;
        }
        default: break;
    }
}

// Пингуем шлюз. Проверяется не мнение драйвера, а факт: ходят ли пакеты.
// Шлюз выбран намеренно — он есть всегда и не зависит от интернета.
// Возвращает false и когда ответа нет, и когда пинг вообще не запустился;
// вторую ситуацию отличает пустой out-параметр possible.
bool pingGateway(bool& possible) {
    possible = false;
    IPAddress gw = WiFi.gatewayIP();
    if ((uint32_t)gw == 0) return false;

    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.target_addr.type = ESP_IPADDR_TYPE_V4;
    cfg.target_addr.u_addr.ip4.addr = (uint32_t)gw;
    cfg.count = 1;
    cfg.timeout_ms = PING_TIMEOUT_MS;

    esp_ping_callbacks_t cbs = {};
    cbs.on_ping_success = [](esp_ping_handle_t, void*) { pingReplied.store(true); };

    esp_ping_handle_t ping = nullptr;
    if (esp_ping_new_session(&cfg, &cbs, &ping) != ESP_OK) return false;

    possible = true;
    pingReplied.store(false);
    esp_ping_start(ping);
    uint32_t start = millis();
    while (!pingReplied.load() && millis() - start < PING_TIMEOUT_MS + 500) delay(10);
    esp_ping_stop(ping);
    esp_ping_delete_session(ping);
    return pingReplied.load();
}

// Возвращает false, если SDK точку не поднял. Проверять обязательно: раньше
// прошивка ставила флаг вслепую и уверяла, что точка есть, когда её не было, —
// а искать в эфире несуществующую сеть можно очень долго.
// Канал, на котором точка вообще сможет вещать. Радио у C3 одно, и если оно
// уже стоит на канале роутера (а оно туда уезжает при каждой попытке
// подключения), то поднимать точку на другом канале бессмысленно: конфиг будет
// говорить одно, приёмопередатчик работать на другом, маяков в эфире не будет.
uint8_t apChannelToUse(const core::Settings& s) {
    uint8_t primary = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    if (esp_wifi_get_channel(&primary, &second) == ESP_OK && primary >= 1 && primary <= 13)
        return primary;
    if (s.channel >= 1 && s.channel <= 13) return s.channel;
    return 1;  // 0 SDK не принимает (waterius ap_channel)
}

bool softApUp(const core::Settings& s) {
    uint8_t channel = apChannelToUse(s);
    if (WiFi.softAP(apName_, nullptr, channel, 0, 4)) {
        apChannel_ = channel;
        // Успех softAP() — это ещё не маяки в эфире. Печатаем то, что радио
        // приняло на самом деле: канал, скрытость, предел клиентов.
        wifi_config_t cfg = {};
        uint8_t primary = 0;
        wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
        esp_wifi_get_config(WIFI_IF_AP, &cfg);
        esp_wifi_get_channel(&primary, &second);
        Log.debug("Wi-Fi: точка в радио — ssid=\"%s\" канал=%u радиоканал=%u скрыта=%u макс=%u режим=%u\n",
                  (const char*)cfg.ap.ssid, (unsigned)cfg.ap.channel, (unsigned)primary,
                  (unsigned)cfg.ap.ssid_hidden, (unsigned)cfg.ap.max_connection,
                  (unsigned)WiFi.getMode());
        return true;
    }
    apChannel_ = 0;
    Log.error("Wi-Fi: точку доступа %s поднять НЕ УДАЛОСЬ\n", apName_);
    return false;
}

void startAp(const core::Settings& s) {
    WiFi.mode(s.ssid[0] ? WIFI_AP_STA : WIFI_AP);
    WiFi.setSleep(false);
    ap_ = softApUp(s);
    if (ap_)
        Log.printf("Wi-Fi: точка доступа %s, канал %u, http://192.168.4.1\n", apName_,
                   (unsigned)apChannel_);
}

void stopAp() {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    ap_ = false;
    apChannel_ = 0;
    Log.println("Wi-Fi: точка доступа выключена");
}

// Статический адрес или DHCP. Запасной DNS ставится в обоих случаях: мёртвый
// DNS роутера — типовая причина «в сети, а в облако не ходит».
void applyIpConfig(const core::Settings& s) {
    if (!s.ip) {
        // Сброс возможной прошлой статики: с этими адресами SDK уходит в DHCP
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
        return;
    }
    IPAddress dns = s.dns ? IPAddress(s.dns) : IPAddress(s.gateway);
    WiFi.config(IPAddress(s.ip), IPAddress(s.gateway), IPAddress(s.mask), dns, FALLBACK_DNS);
}

// DHCP присылает свои серверы и затирает наши, поэтому запасной прописывается
// уже после получения адреса — вторым, основной остаётся роутерский.
void setBackupDns() {
    esp_netif_dns_info_t dns = {};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = (uint32_t)FALLBACK_DNS;
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns);
}

void beginSta(const core::Settings& s, bool fast) {
    applyIpConfig(s);
    // Быстрый коннект бьёт в сохранённые канал и BSSID — без скана эфира.
    // Полный скан медленнее, но находит роутер, который переехал.
    const uint8_t* bssid = fast && s.channel && hasBssid(s.bssid) ? s.bssid : nullptr;
    Log.printf("Wi-Fi: подключаемся к %s (%s)\n", s.ssid, bssid ? "канал и BSSID известны" : "полный скан");
    WiFi.begin(s.ssid, s.pass, bssid ? s.channel : 0, bssid);
}

// Стек Wi-Fi залип: ни одна попытка не доходит до конца. То же делает ESPHome
// в restart_adapter(), прежде чем дойти до перезагрузки всей платы.
void restartRadio(const core::Settings& s) {
    Log.warn("Wi-Fi: перезапуск радио — попытки не проходят\n");
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    delay(RADIO_OFF_MS);
    WiFi.mode(ap_ ? WIFI_AP_STA : WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setHostname(apName_);
    if (ap_) ap_ = softApUp(s);  // после WIFI_OFF точку надо поднимать заново
}

void applyAction(core::WifiAction action, const core::Settings& s) {
    switch (action) {
        case core::WifiAction::ConnectFast: beginSta(s, true); break;
        case core::WifiAction::ConnectScan: beginSta(s, false); break;
        case core::WifiAction::RestartRadio: restartRadio(s); break;
        case core::WifiAction::StartAp: startAp(s); break;
        case core::WifiAction::StopAp: stopAp(); break;
        case core::WifiAction::Reboot:
            Log.warn("Wi-Fi: нет сети %lu минут — перезагрузка\n",
                       (unsigned long)(policy.offlineMs(millis()) / 60000UL));
            rebootWanted = true;
            break;
        case core::WifiAction::None: break;
    }
}

void applyPolicyConfig(const core::Settings& s) {
    core::WifiPolicyCfg cfg;
    cfg.rebootAfterMs = (uint32_t)s.rebootMin * 60000UL;
    if (safeMode_) {
        cfg.apAfterMs = 0;      // страница /update нужна немедленно
        cfg.rebootAfterMs = 0;  // добивать перезагрузками и так сломанную прошивку незачем
    }
    policy.configure(cfg);
}

}  // namespace

void setSafeMode(bool on) { safeMode_ = on; }

void begin(const core::Settings& s) {
    uint64_t mac = ESP.getEfuseMac();
    snprintf(apName_, sizeof(apName_), "esp32-opto-%02X%02X", (unsigned)((mac >> 32) & 0xFF),
             (unsigned)((mac >> 40) & 0xFF));
    tls.setInsecure();  // как в прошивке Waterius: сертификат не проверяем
    WiFi.persistent(false);
    WiFi.onEvent(onWifiEvent);
    WiFi.setHostname(apName_);
    // Два сервера: с одним устройство может остаться без времени навсегда,
    // а время — это метка показаний, уходящая в облако
    configTime(0, 0, "ru.pool.ntp.org", "pool.ntp.org");

    // Переподключением занимается политика: два механизма мешали бы друг другу,
    // а автореконнект SDK умеет молча сдаваться. Ставится до всех ветвлений:
    // флаг статический и общий, а на чистом устройстве (сеть ещё не задана) мы
    // выходим из begin() раньше — и SDK оставался бы с умолчанием true. Тогда
    // на каждый отказ роутера срабатывал бы его собственный реконнект
    // (WiFiGeneric.cpp: `WiFi.getAutoReconnect() && _isReconnectableReason`),
    // и подключение повторялось бы каждые пару секунд поверх нашей лестницы.
    WiFi.setAutoReconnect(false);

    applyPolicyConfig(s);
    policy.reset(millis());
    hasSsid_ = s.ssid[0] != 0;
    if (!hasSsid_) return;  // точку доступа поднимет первый же шаг политики

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // устройство всегда в сети
}

// Каждая попытка подключения уводит радио на канал роутера, а конфиг точки
// остаётся на прежнем — и точка замолкает, оставаясь «поднятой» по всем флагам.
// Возвращаем её на тот канал, где радио сейчас. Найдено на плате: точка стояла
// на канале 1, радио ушло на 6, в эфире сети не было (см. docs/08-reliability).
void followRadioChannel(const core::Settings& s) {
    // Во время полного скана радио перебирает каналы каждые ~120 мс, и
    // мгновенное расхождение — норма, а не повод переподнимать точку. Ждём,
    // пока оно устоится, и не переносим чаще раза в 10 секунд: каждый вызов
    // softAP() переинициализирует интерфейс и прерывает маяки, то есть лечение
    // без выдержки было бы хуже болезни.
    const uint32_t SETTLE_MS = 3000;
    const uint32_t MOVE_GAP_MS = 10000;
    static uint32_t mismatchSinceMs = 0;  // 0 — расхождения нет
    static uint32_t lastMoveMs = 0;

    if (!apActive()) {
        mismatchSinceMs = 0;
        return;
    }
    uint8_t primary = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    wifi_config_t cfg = {};
    if (esp_wifi_get_channel(&primary, &second) != ESP_OK || !primary ||
        esp_wifi_get_config(WIFI_IF_AP, &cfg) != ESP_OK || cfg.ap.channel == primary) {
        mismatchSinceMs = 0;
        return;
    }

    uint32_t now = millis();
    if (!mismatchSinceMs) {
        mismatchSinceMs = now | 1;  // 0 занят под «расхождения нет»
        return;
    }
    if (now - mismatchSinceMs < SETTLE_MS) return;
    if (lastMoveMs && now - lastMoveMs < MOVE_GAP_MS) return;

    Log.warn("Wi-Fi: точка переезжает с канала %u на %u — радио ушло за роутером\n",
             (unsigned)cfg.ap.channel, (unsigned)primary);
    if (WiFi.softAP(apName_, nullptr, primary, 0, 4)) apChannel_ = primary;
    mismatchSinceMs = 0;
    lastMoveMs = now | 1;
}

void loop(const core::Settings& s) {
    applyPolicyConfig(s);
    followRadioChannel(s);
    hasSsid_ = s.ssid[0] != 0;
    uint32_t now = millis();

    // Сначала мнение драйвера: смена состояния обнуляет контроль живости,
    // иначе после переподключения он сразу вынес бы прошлый приговор
    bool driverUp = driverConnected();
    if (driverUp != wasConnected) {
        link.reset(now);
        linkDeadLogged = false;
    }

    // Пакеты реально ходят? Драйвер об этом знать не обязан
    if (driverUp && link.shouldProbe(now)) {
        bool wasArmed = link.armed();
        bool possible = false;
        bool replied = pingGateway(possible);
        now = millis();  // пинг занимает до двух секунд
        if (replied) link.probeOk(now);
        else if (possible) link.probeFailed(now);

        // Про удачную проверку сказать надо один раз: пока она не прошла,
        // сторож не взведён и связь мёртвой не объявит. Молча — значит
        // снаружи не отличить работающий контроль от бездействующего.
        if (replied && !wasArmed) Log.println("Сеть: проверка связи работает, шлюз отвечает");
        if (!possible && !probeImpossibleLogged) {
            probeImpossibleLogged = true;
            Log.warn("Сеть: проверку связи запустить не удалось — контроль живости выключен\n");
        }
        if (!replied && possible && link.armed())
            Log.warn("Сеть: шлюз не отвечает (%u подряд)\n", (unsigned)link.failures());
    }
    if (link.dead() && !linkDeadLogged) {
        linkDeadLogged = true;
        Log.warn("Сеть: связь мертва, хотя драйвер считает иначе — переподключаемся\n");
    }

    bool up = driverUp && !link.dead();
    if (up && !wasConnected) {
        wasConnected = true;
        fastConnectFresh = true;
        everConnected = true;
        error_[0] = 0;
        lastReason_.store(0);
        setBackupDns();
        Log.printf("Wi-Fi: подключено к %s, IP %s, RSSI %d\n", WiFi.SSID().c_str(),
                   WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else if (!driverUp && wasConnected) {
        wasConnected = false;
        Log.warn("Wi-Fi: связь с роутером потеряна\n");
    }
    wasConnected = driverUp;

    // Сеть ввели только что, и роутер отказал: это ответ, а не молчание.
    // Уже работавшую сеть это не трогает — там отказ бывает и при перезагрузке роутера.
    bool refusedNew = !everConnected && refused(lastReason_.load());
    if (refusedNew && !error_[0]) {
        snprintf(error_, sizeof(error_), "%s",
                 lastReason_.load() == WIFI_REASON_NO_AP_FOUND ? "сеть не найдена" : "роутер отверг пароль");
        Log.warn("Wi-Fi: %s\n", error_);
    }

    core::WifiFacts facts;
    facts.refusedNewNetwork = refusedNew;
    facts.linkUp = up;  // мёртвый линк для лестницы — обычный обрыв
    facts.haveSsid = hasSsid_;
    facts.haveFastConnect = s.channel && hasBssid(s.bssid);
    facts.apActive = ap_;
    facts.apBusy = ap_ && WiFi.softAPgetStationNum() > 0;
    facts.portalIdleMs = portalIdleMs();
    applyAction(policy.step(facts, now), s);
}

void reconnect(const core::Settings& s) {
    WiFi.mode(ap_ ? WIFI_AP_STA : WIFI_STA);
    WiFi.setSleep(false);
    WiFi.disconnect();
    gotIp_.store(false);
    wasConnected = false;
    hasSsid_ = s.ssid[0] != 0;
    everConnected = false;  // пароль новый: отказ по нему снова поднимает точку сразу
    error_[0] = 0;
    lastReason_.store(0);
    link.reset(millis());
    linkDeadLogged = false;
    policy.reset(millis());  // лестница начинается заново, попытка — сразу
}

// Наружу — связь, подтверждённая делом. Мёртвый линк, который драйвер считает
// живым, для всей прошивки выглядит как отсутствие связи, и лестница
// восстановления берётся за него как за обычный обрыв.
bool connected() { return driverConnected() && !link.dead(); }

bool linkAlive() { return !link.dead(); }
bool linkGuardArmed() { return link.armed(); }
// Флага мало: SDK мог точку не поднять или снять её при смене режима
bool apActive() { return ap_ && (WiFi.getMode() & WIFI_MODE_AP) != 0; }
// Канал спрашиваем у радио, а не у своей переменной. В режиме AP+STA канал
// точки обязан совпадать с каналом станции: подключение к роутеру уводит радио
// на его канал, и точка уезжает следом. Своё сохранённое значение в этот момент
// врёт — та же ошибка, что и флаг ap_, который мы уже чинили.
uint8_t apChannel() {
    if (!apActive()) return 0;
    uint8_t primary = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    if (esp_wifi_get_channel(&primary, &second) == ESP_OK && primary) return primary;
    return apChannel_;
}

// Канал, который радио приняло в конфиг точки. Отличается от apChannel()
// намеренно: тот отдаёт рабочий канал приёмопередатчика. Расхождение этих двух
// чисел и означает «точка поднята, а маяков нет».
uint8_t apConfigChannel() {
    if (!apActive()) return 0;
    wifi_config_t cfg = {};
    if (esp_wifi_get_config(WIFI_IF_AP, &cfg) != ESP_OK) return 0;
    return cfg.ap.channel;
}

uint8_t apClients() { return apActive() ? WiFi.softAPgetStationNum() : 0; }

// Окно портала. Продлевают только действия человека — открытие страниц,
// сохранение формы, нажатия кнопок. Опрос /api/status и /api/log сюда
// намеренно не входит: страницы опрашивают их сами раз в секунду, и открытая
// вкладка держала бы портал вечно (та же причина, что в waterius, issue #305).
// Пишется из задачи async_tcp, читается из loop() — отсюда atomic.
void feedPortal() { portalFedMs_.store(millis()); }

uint32_t portalIdleMs() { return millis() - portalFedMs_.load(); }

const char* error() { return error_; }

Status status() {
    if (connected()) return Status::Connected;
    if (!hasSsid_) return Status::Idle;
    return (policy.failures() || error_[0]) ? Status::Failed : Status::Connecting;
}

const char* modeName() {
    if (!ap_) return "STA";
    return WiFi.getMode() == WIFI_AP ? "AP" : "AP+STA";
}

const char* apName() { return apName_; }

String ip() {
    if (connected()) return WiFi.localIP().toString();
    if (ap_) return WiFi.softAPIP().toString();
    return String();
}

int rssi() { return connected() ? WiFi.RSSI() : 0; }
uint32_t chipId() { return (uint32_t)(ESP.getEfuseMac() & 0xFFFFFF); }

bool takeFastConnect(uint8_t& channel, uint8_t bssid[6]) {
    if (!fastConnectFresh || !connected()) return false;
    fastConnectFresh = false;
    channel = (uint8_t)WiFi.channel();
    memcpy(bssid, WiFi.BSSID(), 6);
    return true;
}

bool takeForgetFastConnect() { return policy.takeForgetFastConnect(); }

bool rebootRequested() { return rebootWanted; }

uint16_t lastDisconnectReason() { return lastReason_.load(); }
uint32_t disconnectCount() { return disconnects_.load(); }
uint32_t offlineSeconds() { return policy.offlineMs(millis()) / 1000; }

WiFiClientSecure& tlsClient() { return tls; }

int postJson(const core::Settings& s, const char* path, const char* body, String& response) {
    if (!connected()) return -1;

    String url = String(s.host);
    if (url.endsWith("/")) url.remove(url.length() - 1);
    url += path;

    // plain объявлен раньше http, чтобы на выходе из функции уничтожался позже —
    // иначе на ветке раннего выхода деструктор HTTPClient трогал бы уже мёртвый WiFiClient.
    WiFiClient plain;
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    bool ok = url.startsWith("https://") ? http.begin(tls, url) : http.begin(plain, url);
    if (!ok) return -2;

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Waterius-Token", s.key);
    http.addHeader("Waterius-Email", s.email);
    int code = http.POST((uint8_t*)body, strlen(body));
    if (code > 0) {
        link.activity(millis());  // сервер ответил — сеть точно жива
        response = http.getString();
    }
    http.end();
    return code;
}

}  // namespace net
