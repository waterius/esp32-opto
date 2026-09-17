#include "net.h"

#include <HTTPClient.h>
#include <WiFi.h>

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
        Log.println("Wi-Fi: связь с роутером потеряна");
    }
    if (status_ == Status::Connecting && now - connectStartMs >= CONNECT_TIMEOUT_MS) status_ = Status::Failed;

    if (!s.ssid[0]) {
        if (!ap_) startAp(s);
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
    wasConnected = false;
    if (!lostSinceMs) lostSinceMs = millis();
}

bool connected() { return WiFi.status() == WL_CONNECTED; }
bool apActive() { return ap_; }
Status status() { return status_; }

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

    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    WiFiClient plain;
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
