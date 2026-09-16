#include "net.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

namespace net {
namespace {

bool ap_ = false;
uint32_t lastTry_ = 0;
const char* AP_NAME = "esp32-opto";

}  // namespace

void begin(const Settings& s) {
    if (s.ssid[0] == 0) {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_NAME);
        ap_ = true;
        return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // плата всегда в сети, сон не нужен
    WiFi.begin(s.ssid, s.pass);
    for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; i++) delay(500);
    if (WiFi.status() != WL_CONNECTED) {
        // Не достучались — поднимаем точку доступа, чтобы можно было
        // зайти на веб-страницу и поправить настройки.
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(AP_NAME);
        ap_ = true;
        return;
    }
    configTime(0, 0, "ru.pool.ntp.org");
}

void loop() {
    if (ap_ || WiFi.status() == WL_CONNECTED) return;
    if (millis() - lastTry_ < 30000) return;
    lastTry_ = millis();
    WiFi.reconnect();
}

bool connected() { return WiFi.status() == WL_CONNECTED; }
bool isAp() { return ap_; }
String ip() { return ap_ && !connected() ? WiFi.softAPIP().toString() : WiFi.localIP().toString(); }
int rssi() { return WiFi.RSSI(); }
uint32_t chipId() { return (uint32_t)(ESP.getEfuseMac() & 0xFFFFFF); }

int postJson(const Settings& s, const char* path, const char* body, String& response) {
    if (!connected()) return -1;

    String url = String(s.host);
    if (url.endsWith("/")) url.remove(url.length() - 1);
    url += path;

    HTTPClient http;
    http.setTimeout(12000);
    bool ok;
    WiFiClientSecure secure;
    WiFiClient plain;
    if (url.startsWith("https://")) {
        secure.setInsecure();  // как в прошивке Waterius: сертификат не проверяем
        ok = http.begin(secure, url);
    } else {
        ok = http.begin(plain, url);
    }
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
