#include "net.h"

#include <HTTPClient.h>
#include <WiFi.h>

#include <atomic>

#include "log.h"

namespace net {
namespace {

const uint32_t AP_AFTER_MS = 2UL * 60 * 1000;      // нет роутера 2 минуты → точка доступа
const uint32_t AP_RETRY_MS = 60UL * 1000;          // при поднятой AP — попытка раз в минуту
const uint32_t CONNECT_TIMEOUT_MS = 20UL * 1000;   // для статуса на странице /wifi
const uint32_t HTTP_TIMEOUT_MS = 12000;            // как SERVER_TIMEOUT в waterius

WiFiClientSecure tls;
char apName_[24] = "";
bool ap_ = false;
bool wasConnected = false;
bool fastConnectFresh = false;
uint32_t lostSinceMs = 0;
uint32_t lastTryMs = 0;
uint32_t connectStartMs = 0;
Status status_ = Status::Idle;
char error_[40] = "";
bool everConnected = false;  // с текущими настройками сети хоть раз подключились

// Причина последнего отказа: пишет задача событий Wi-Fi, читает loop().
// Спрашивать WiFi.status() бесполезно: на первой попытке arduino-esp32 не
// выставляет WL_CONNECT_FAILED даже при AUTH_FAIL (WiFiGeneric.cpp, !first_connect),
// а молча уходит в переподключение — отказ по паролю не отличить от молчания.
std::atomic<uint8_t> lastReason{0};

void onDisconnected(WiFiEvent_t, WiFiEventInfo_t info) {
    lastReason.store(info.wifi_sta_disconnected.reason);
}

// Роутер ответил отказом — ждать больше нечего.
bool refused(uint8_t reason) {
    return reason == WIFI_REASON_AUTH_EXPIRE || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
           reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_HANDSHAKE_TIMEOUT ||
           reason == WIFI_REASON_NO_AP_FOUND;
}

bool hasBssid(const uint8_t bssid[6]) {
    for (int i = 0; i < 6; ++i)
        if (bssid[i]) return true;
    return false;
}

void beginSta(const core::Settings& s) {
    // Канал и BSSID известны — подключаемся без полного скана (waterius wifi_begin)
    const uint8_t* bssid = s.channel && hasBssid(s.bssid) ? s.bssid : nullptr;
    WiFi.begin(s.ssid, s.pass, bssid ? s.channel : 0, bssid);
    lastTryMs = millis();
}

void startAp(const core::Settings& s) {
    WiFi.mode(s.ssid[0] ? WIFI_AP_STA : WIFI_AP);
    WiFi.setSleep(false);
    // Одно радио на оба режима: канал AP = канал роутера; 0 SDK не принимает (waterius ap_channel)
    uint8_t channel = s.channel >= 1 && s.channel <= 13 ? s.channel : 1;
    WiFi.softAP(apName_, nullptr, channel, 0, 4);
    WiFi.setAutoReconnect(false);  // при поднятой AP переподключаемся сами, раз в минуту
    ap_ = true;
    Log.printf("Wi-Fi: точка доступа %s, http://192.168.4.1\n", apName_);
}

void stopAp() {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    ap_ = false;
    Log.println("Wi-Fi: точка доступа выключена");
}

}  // namespace

void begin(const core::Settings& s) {
    uint64_t mac = ESP.getEfuseMac();
    snprintf(apName_, sizeof(apName_), "esp32-opto-%02X%02X", (unsigned)((mac >> 32) & 0xFF),
             (unsigned)((mac >> 40) & 0xFF));
    tls.setInsecure();  // как в прошивке Waterius: сертификат не проверяем
    WiFi.onEvent(onDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.persistent(false);
    WiFi.setHostname(apName_);
    configTime(0, 0, "ru.pool.ntp.org");

    lostSinceMs = millis();
    if (!s.ssid[0]) {
        startAp(s);
        return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // устройство всегда в сети
    WiFi.setAutoReconnect(true);
    beginSta(s);
    status_ = Status::Connecting;
    connectStartMs = millis();
}

void loop(const core::Settings& s) {
    uint32_t now = millis();

    if (WiFi.status() == WL_CONNECTED) {
        if (!wasConnected) {
            wasConnected = true;
            fastConnectFresh = true;
            status_ = Status::Connected;
            error_[0] = 0;
            everConnected = true;
            lastReason.store(0);
            lostSinceMs = 0;
            Log.printf("Wi-Fi: подключено к %s, IP %s\n", WiFi.SSID().c_str(),
                          WiFi.localIP().toString().c_str());
        }
        // AP гасится, только когда к ней никто не подключён
        if (ap_ && WiFi.softAPgetStationNum() == 0) stopAp();
        return;
    }

    if (wasConnected) {
        wasConnected = false;
        lostSinceMs = now;
        status_ = Status::Failed;  // иначе /api/wifi_status ещё до AP_AFTER_MS отвечает «подключено»
        Log.println("Wi-Fi: связь с роутером потеряна");
    }
    if (status_ == Status::Connecting && now - connectStartMs >= CONNECT_TIMEOUT_MS) status_ = Status::Failed;

    if (!s.ssid[0]) {
        if (!ap_) startAp(s);
        return;
    }
    // Сеть ввели только что, и роутер отказал — это ответ, а не молчание: ждать
    // AP_AFTER_MS незачем, пользователю прямо сейчас нужна страница /wifi.
    // Уже работавшую сеть это не трогает: там отказ бывает и при перезагрузке роутера.
    uint8_t reason = lastReason.load();
    if (!ap_ && !everConnected && refused(reason)) {
        snprintf(error_, sizeof(error_), "%s",
                 reason == WIFI_REASON_NO_AP_FOUND ? "сеть не найдена" : "роутер отверг пароль");
        status_ = Status::Failed;
        Log.printf("Wi-Fi: %s (код %u)\n", error_, (unsigned)reason);
        startAp(s);
        return;
    }
    if (!ap_ && now - lostSinceMs >= AP_AFTER_MS) startAp(s);
    if (ap_ && now - lastTryMs >= AP_RETRY_MS) {
        WiFi.disconnect();
        beginSta(s);
    }
}

void reconnect(const core::Settings& s) {
    WiFi.mode(ap_ ? WIFI_AP_STA : WIFI_STA);
    WiFi.setSleep(false);
    WiFi.disconnect();
    beginSta(s);
    status_ = Status::Connecting;
    connectStartMs = millis();
    error_[0] = 0;
    everConnected = false;  // пароль новый: отказ по нему снова поднимает точку сразу
    lastReason.store(0);
    wasConnected = false;
    if (!lostSinceMs) lostSinceMs = millis();
}

bool connected() { return WiFi.status() == WL_CONNECTED; }
bool apActive() { return ap_; }
Status status() { return status_; }
const char* error() { return error_; }

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
