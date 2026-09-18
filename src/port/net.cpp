#include "net.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_netif.h>

#include <atomic>

#include "../core/wifi_policy.h"
#include "log.h"

namespace net {
namespace {

const uint32_t HTTP_TIMEOUT_MS = 12000;  // как SERVER_TIMEOUT в waterius
const uint32_t RADIO_OFF_MS = 200;       // пауза между выключением и включением радио
const IPAddress FALLBACK_DNS(8, 8, 8, 8);  // как DEF_FALLBACK_DNS в waterius

core::WifiPolicy policy;
WiFiClientSecure tls;
char apName_[24] = "";
bool ap_ = false;
bool wasConnected = false;
bool fastConnectFresh = false;
bool rebootWanted = false;
bool hasSsid_ = false;
bool everConnected = false;  // с текущими настройками сети хоть раз подключились
bool safeMode_ = false;
char error_[40] = "";        // причина отказа для страницы /wifi

// Пишет колбэк событий SDK (задача event loop), читает loop().
std::atomic<bool> gotIp_{false};
std::atomic<uint16_t> lastReason_{0};
std::atomic<uint32_t> disconnects_{0};

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
        case 15: return "не сошёлся пароль (4-way handshake)";
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
            Log.printf("Wi-Fi: разрыв, причина %u — %s\n", reason, reasonName(reason));
            break;
        }
        default: break;
    }
}

void softApUp(const core::Settings& s) {
    // Одно радио на оба режима: канал AP = канал роутера; 0 SDK не принимает (waterius ap_channel)
    uint8_t channel = s.channel >= 1 && s.channel <= 13 ? s.channel : 1;
    WiFi.softAP(apName_, nullptr, channel, 0, 4);
}

void startAp(const core::Settings& s) {
    WiFi.mode(s.ssid[0] ? WIFI_AP_STA : WIFI_AP);
    WiFi.setSleep(false);
    softApUp(s);
    ap_ = true;
    Log.printf("Wi-Fi: точка доступа %s, http://192.168.4.1\n", apName_);
}

void stopAp() {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    ap_ = false;
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
    Log.println("Wi-Fi: перезапуск радио — попытки не проходят");
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    delay(RADIO_OFF_MS);
    WiFi.mode(ap_ ? WIFI_AP_STA : WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setHostname(apName_);
    if (ap_) softApUp(s);
}

void applyAction(core::WifiAction action, const core::Settings& s) {
    switch (action) {
        case core::WifiAction::ConnectFast: beginSta(s, true); break;
        case core::WifiAction::ConnectScan: beginSta(s, false); break;
        case core::WifiAction::RestartRadio: restartRadio(s); break;
        case core::WifiAction::StartAp: startAp(s); break;
        case core::WifiAction::StopAp: stopAp(); break;
        case core::WifiAction::Reboot:
            Log.printf("Wi-Fi: нет сети %lu минут — перезагрузка\n",
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

void loop(const core::Settings& s) {
    applyPolicyConfig(s);
    hasSsid_ = s.ssid[0] != 0;

    bool up = connected();
    if (up && !wasConnected) {
        wasConnected = true;
        fastConnectFresh = true;
        everConnected = true;
        error_[0] = 0;
        lastReason_.store(0);
        setBackupDns();
        Log.printf("Wi-Fi: подключено к %s, IP %s, RSSI %d\n", WiFi.SSID().c_str(),
                   WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else if (!up && wasConnected) {
        wasConnected = false;
        Log.println("Wi-Fi: связь с роутером потеряна");
    }

    // Сеть ввели только что, и роутер отказал: это ответ, а не молчание.
    // Уже работавшую сеть это не трогает — там отказ бывает и при перезагрузке роутера.
    bool refusedNew = !everConnected && refused(lastReason_.load());
    if (refusedNew && !error_[0]) {
        snprintf(error_, sizeof(error_), "%s",
                 lastReason_.load() == WIFI_REASON_NO_AP_FOUND ? "сеть не найдена" : "роутер отверг пароль");
        Log.printf("Wi-Fi: %s — поднимаем точку доступа\n", error_);
    }

    core::WifiFacts facts;
    facts.refusedNewNetwork = refusedNew;
    facts.linkUp = up;
    facts.haveSsid = hasSsid_;
    facts.haveFastConnect = s.channel && hasBssid(s.bssid);
    facts.apActive = ap_;
    facts.apBusy = ap_ && WiFi.softAPgetStationNum() > 0;
    applyAction(policy.step(facts, millis()), s);
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
    policy.reset(millis());  // лестница начинается заново, попытка — сразу
}

bool connected() { return gotIp_.load() && WiFi.status() == WL_CONNECTED; }
bool apActive() { return ap_; }

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
    if (code > 0) response = http.getString();
    http.end();
    return code;
}

}  // namespace net
