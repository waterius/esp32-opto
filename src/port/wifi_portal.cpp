#include "wifi_portal.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <WiFi.h>

#include <atomic>

#include "../app.h"
#include "log.h"
#include "net.h"
#include "storage.h"

namespace wifi_portal {
namespace {

const char* PORTAL_URL = "http://192.168.4.1/wifi.html";

DNSServer dns;
bool dnsStarted = false;

// Новая сеть из формы: пишет обработчик (поток async_tcp), применяет loop().
struct PendingWifi {
    char ssid[33];
    char pass[65];
    uint8_t channel;
    uint8_t bssid[6];
};
PendingWifi pending;
std::atomic<bool> pendingFlag{false};

// --- разбор скрытых полей формы: waterius ESP8266/src/core/wifi.cpp ---

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Ноль — канал неизвестен, идём полным сканом.
uint8_t parseWifiChannel(const char* value) {
    long channel = atol(value);
    return channel >= 1 && channel <= 13 ? (uint8_t)channel : 0;
}

// aa:bb:cc:dd:ee:ff, aa-bb-... или 12 hex-символов подряд. При ошибке — нули.
bool parseBssid(const char* value, uint8_t out[6]) {
    memset(out, 0, 6);
    uint8_t bytes[6] = {0};
    int digits = 0;
    for (size_t i = 0; value[i]; ++i) {
        char c = value[i];
        if (c == ':' || c == '-') continue;
        int d = hexDigit(c);
        if (d < 0 || digits >= 12) return false;
        bytes[digits / 2] = (uint8_t)(bytes[digits / 2] << 4 | d);
        digits++;
    }
    if (digits != 12) return false;
    memcpy(out, bytes, 6);
    return true;
}

bool hasBssid(const uint8_t bssid[6]) {
    for (int i = 0; i < 6; ++i)
        if (bssid[i]) return true;
    return false;
}

// --- обработчики ---

void sendJson(AsyncWebServerRequest* request, JsonDocument& doc) {
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

String param(AsyncWebServerRequest* request, const char* name) {
    return request->hasParam(name, true) ? request->getParam(name, true)->value() : String();
}

// Список сетей (waterius get_api_networks). Скан асинхронный: пока идёт,
// отвечаем {"scanning":true}, страница переспрашивает.
void getNetworks(AsyncWebServerRequest* request) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
        request->send(200, "application/json", "{\"scanning\":true}");
        return;
    }
    if (n < 0) {
        WiFi.scanNetworks(true);
        request->send(200, "application/json", "{\"scanning\":true}");
        return;
    }
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < n; ++i) {
        JsonObject o = arr.add<JsonObject>();
        o["ssid"] = WiFi.SSID(i);
        long level = map(WiFi.RSSI(i), -100, -50, 1, 4);
        o["level"] = level < 1 ? 1 : (level > 4 ? 4 : level);
        // Канал и BSSID именно этой сети: форма вернёт их, и коннект пойдёт без скана
        o["wifi_channel"] = WiFi.channel(i);
        o["bssid"] = WiFi.BSSIDstr(i);
        o["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();
    sendJson(request, doc);
}

// Новая сеть (waterius post_api_save_connect + save_fast_connect).
void postWifi(AsyncWebServerRequest* request) {
    JsonDocument doc;
    JsonObject errors = doc["errors"].to<JsonObject>();
    String ssid = param(request, "ssid");
    String pass = param(request, "password");
    if (ssid.isEmpty() || ssid.length() > 32) errors["ssid"] = "Введите название сети, до 32 символов";
    if (pass.length() > 64 || (pass.length() > 0 && pass.length() < 8))
        errors["password"] = "Пароль — от 8 до 64 символов, пусто для открытой сети";
    if (errors.size()) {
        sendJson(request, doc);
        return;
    }

    PendingWifi p = {};
    snprintf(p.ssid, sizeof(p.ssid), "%s", ssid.c_str());
    snprintf(p.pass, sizeof(p.pass), "%s", pass.c_str());
    // Пара пишется целиком или не пишется: канал без BSSID хуже полного скана
    p.channel = parseWifiChannel(param(request, "wifi_channel").c_str());
    if (!p.channel || !parseBssid(param(request, "bssid").c_str(), p.bssid) || !hasBssid(p.bssid)) {
        p.channel = 0;
        memset(p.bssid, 0, sizeof(p.bssid));
    }
    pending = p;
    pendingFlag.store(true);

    doc.remove("errors");
    doc["ok"] = true;
    sendJson(request, doc);
}

void getWifiStatus(AsyncWebServerRequest* request) {
    static const char* NAMES[] = {"idle", "connecting", "connected", "failed"};
    JsonDocument doc;
    doc["status"] = NAMES[(int)net::status()];
    doc["error"] = net::error();
    doc["ssid"] = app.sett.ssid;
    doc["ip"] = net::ip();
    doc["rssi"] = net::rssi();
    doc["mode"] = net::modeName();
    sendJson(request, doc);
}

void redirectToPortal(AsyncWebServerRequest* request) { request->redirect(PORTAL_URL); }

}  // namespace

void registerRoutes(AsyncWebServer& server) {
    // Скан сетей и «Подключиться» — это действия человека, они продлевают окно
    // портала. /api/wifi_status страница опрашивает сама, пока идёт попытка, и
    // окно продлевать не должен — иначе открытая вкладка держала бы портал.
    server.on("/api/networks", HTTP_GET, [](AsyncWebServerRequest* request) {
        net::feedPortal();
        getNetworks(request);
    });
    server.on("/api/wifi", HTTP_POST, [](AsyncWebServerRequest* request) {
        net::feedPortal();
        postWifi(request);
    });
    server.on("/api/wifi_status", HTTP_GET, getWifiStatus);

    // Captive portal — проверки ОС из waterius active_point.cpp, только для клиентов AP
    server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("http://logout.net");  // обход для Windows 11
    }).setFilter(ON_AP_FILTER);
    server.on("/wpad.dat", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(404);  // иначе Windows 10 спрашивает его бесконечно
    }).setFilter(ON_AP_FILTER);
    server.on("/generate_204", HTTP_GET, redirectToPortal).setFilter(ON_AP_FILTER);        // Android
    server.on("/redirect", HTTP_GET, redirectToPortal).setFilter(ON_AP_FILTER);            // Microsoft
    server.on("/hotspot-detect.html", HTTP_GET, redirectToPortal).setFilter(ON_AP_FILTER); // Apple
    server.on("/canonical.html", HTTP_GET, redirectToPortal).setFilter(ON_AP_FILTER);      // Firefox
    server.on("/success.txt", HTTP_GET, redirectToPortal).setFilter(ON_AP_FILTER);         // Firefox
    server.on("/ncsi.txt", HTTP_GET, redirectToPortal).setFilter(ON_AP_FILTER);            // Windows
    server.on("/fwlink", HTTP_GET, redirectToPortal).setFilter(ON_AP_FILTER);              // Microsoft
}

void loop() {
    if (net::apActive() && !dnsStarted) {
        dns.start(53, "*", WiFi.softAPIP());
        dnsStarted = true;
    } else if (!net::apActive() && dnsStarted) {
        dns.stop();
        dnsStarted = false;
    }
    if (dnsStarted) dns.processNextRequest();

    if (pendingFlag.exchange(false)) {
        PendingWifi p = pending;
        memcpy(app.sett.ssid, p.ssid, sizeof(app.sett.ssid));
        memcpy(app.sett.pass, p.pass, sizeof(app.sett.pass));
        memcpy(app.sett.bssid, p.bssid, sizeof(app.sett.bssid));
        app.sett.channel = p.channel;
        if (!storage::saveSettings(app.sett))
            Log.error("Wi-Fi: сеть не сохранилась, после перезагрузки вернётся прежняя\n");
        storage::saveFastConnect(app.sett);  // пара лежит отдельным ключом
        Log.printf("Wi-Fi: новая сеть %s\n", app.sett.ssid);
        net::reconnect(app.sett);
    }
}

}  // namespace wifi_portal
