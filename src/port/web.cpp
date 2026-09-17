#include "web.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include <memory>

#include "../app.h"
#include "../poller.h"
#include "log.h"
#include "net.h"
#include "wifi_portal.h"

namespace web {
namespace {

AsyncWebServer server(80);

const long BAUDS[] = {300, 600, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};

void sendJson(AsyncWebServerRequest* request, JsonDocument& doc) {
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void sendOk(AsyncWebServerRequest* request) { request->send(200, "application/json", "{\"ok\":true}"); }

void getStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["fw"] = FIRMWARE_VERSION;
    doc["ip"] = net::ip();
    doc["rssi"] = net::rssi();
    doc["uptime_s"] = millis() / 1000;
    doc["heap"] = ESP.getFreeHeap();
    doc["wifi_mode"] = net::modeName();

    doc["meter_enabled"] = app.sett.meterEnabled;
    doc["meter_reading"] = app.meterReading.load();
    doc["meter_error"] = app.meterError;

    doc["has_reading"] = app.hasReading;
    doc["read_at"] = app.lastReadAt;
    doc["serial"] = app.last.serial;
    doc["model"] = app.last.model;
    doc["meter_fw"] = app.last.fwVersion;
    doc["meter_time"] = app.last.time;
    doc["total"] = app.last.total;
    JsonArray tariffs = doc["tariffs"].to<JsonArray>();
    for (uint8_t i = 0; i < app.last.tariffCount && i < core::MAX_TARIFFS; ++i) tariffs.add(app.last.tariff[i]);

    doc["cloud_at"] = app.cloudAt;
    doc["cloud_code"] = app.cloudCode;
    doc["cloud_error"] = app.cloudError;
    doc["cloud_next_s"] = poller::secondsToNextSend();
    sendJson(request, doc);
}

void getSettings(AsyncWebServerRequest* request) {
    const core::Settings& s = app.sett;
    JsonDocument doc;
    doc["baud"] = s.serial.baud;
    doc["bits"] = s.serial.bits;
    char parity[2] = {s.serial.parity, 0};
    doc["parity"] = parity;
    doc["stop"] = s.serial.stop;
    doc["meter_enabled"] = s.meterEnabled;
    doc["meter_addr"] = s.meterAddr;
    doc["meter_pwd"] = s.meterPwd;
    doc["period_min"] = s.periodMin;
    doc["host"] = s.host;
    doc["key"] = s.key;
    doc["email"] = s.email;
    doc["rfc_enabled"] = s.rfcEnabled;
    doc["rfc_port"] = s.rfcPort;
    sendJson(request, doc);
}

String param(AsyncWebServerRequest* request, const char* name) {
    String v = request->hasParam(name, true) ? request->getParam(name, true)->value() : String();
    v.trim();
    return v;
}

// Целое в диапазоне; иначе ошибка поля.
bool paramLong(AsyncWebServerRequest* request, const char* name, long lo, long hi, long& out,
               JsonObject errors, const char* message) {
    String v = param(request, name);
    char* end = nullptr;
    long x = strtol(v.c_str(), &end, 10);
    if (v.isEmpty() || *end || x < lo || x > hi) {
        errors[name] = message;
        return false;
    }
    out = x;
    return true;
}

// Строка, влезающая в буфер вместе с нулём.
bool paramStr(AsyncWebServerRequest* request, const char* name, char* dst, size_t cap,
              JsonObject errors, const char* message) {
    String v = param(request, name);
    if (v.length() >= cap) {
        errors[name] = message;
        return false;
    }
    snprintf(dst, cap, "%s", v.c_str());
    return true;
}

// Чекбокс: formSubmit шлёт 1 или 0, как в портале waterius.
bool paramBool(AsyncWebServerRequest* request, const char* name) { return param(request, name) == "1"; }

void postSettings(AsyncWebServerRequest* request) {
    core::Settings s = app.sett;
    JsonDocument doc;
    JsonObject errors = doc["errors"].to<JsonObject>();
    long v = 0;

    if (paramLong(request, "baud", 300, 115200, v, errors, "Выберите скорость из списка")) {
        bool known = false;
        for (long b : BAUDS) known = known || b == v;
        if (known) s.serial.baud = (uint32_t)v;
        else errors["baud"] = "Выберите скорость из списка";
    }
    if (paramLong(request, "bits", 5, 8, v, errors, "Биты данных: от 5 до 8")) s.serial.bits = (uint8_t)v;
    String parity = param(request, "parity");
    if (parity == "N" || parity == "E" || parity == "O") s.serial.parity = parity[0];
    else errors["parity"] = "Выберите чётность";
    if (paramLong(request, "stop", 1, 2, v, errors, "Стоп-биты: 1 или 2")) s.serial.stop = (uint8_t)v;

    s.meterEnabled = paramBool(request, "meter_enabled");
    if (paramLong(request, "meter_addr", 0, 127, v, errors, "Адрес: от 0 до 127")) s.meterAddr = (uint8_t)v;
    paramStr(request, "meter_pwd", s.meterPwd, sizeof(s.meterPwd), errors, "Пароль — до 16 символов");

    if (paramLong(request, "period_min", 1, 1440, v, errors, "Период: от 1 до 1440 минут")) s.periodMin = (uint16_t)v;
    if (paramStr(request, "host", s.host, sizeof(s.host), errors, "Адрес сервера — до 63 символов") &&
        strncmp(s.host, "http://", 7) != 0 && strncmp(s.host, "https://", 8) != 0)
        errors["host"] = "Адрес начинается с http:// или https://";
    paramStr(request, "key", s.key, sizeof(s.key), errors, "Ключ — до 40 символов");
    paramStr(request, "email", s.email, sizeof(s.email), errors, "E-mail — до 63 символов");

    s.rfcEnabled = paramBool(request, "rfc_enabled");
    if (paramLong(request, "rfc_port", 1, 65535, v, errors, "Порт: от 1 до 65535")) s.rfcPort = (uint16_t)v;

    if (errors.size()) {
        sendJson(request, doc);
        return;
    }

    bool reboot = s.rfcEnabled != app.sett.rfcEnabled || s.rfcPort != app.sett.rfcPort;
    app.pendingSettings = s;
    app.settingsPending.store(true);

    doc.remove("errors");
    doc["ok"] = true;
    doc["reboot"] = reboot;
    sendJson(request, doc);
}

// Текст лога после позиции from; страница log.html опрашивает раз в секунду.
void getLog(AsyncWebServerRequest* request) {
    uint32_t from = request->hasParam("from") ? strtoul(request->getParam("from")->value().c_str(), nullptr, 10) : 0;
    std::unique_ptr<char[]> text(new char[LogSink::SIZE + 1]);
    size_t len = 0;
    bool skipped = false;
    uint32_t next = Log.read(from, text.get(), LogSink::SIZE, len, skipped);
    text[len] = 0;

    JsonDocument doc;
    doc["boot"] = Log.bootId();
    doc["next"] = next;
    doc["skipped"] = skipped;
    doc["text"] = (const char*)text.get();  // const char* — без копии в документ
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

}  // namespace

void begin() {
    if (!LittleFS.begin()) Log.println("LittleFS не смонтирован: залейте образ командой uploadfs");

    server.on("/api/status", HTTP_GET, getStatus);
    server.on("/api/settings", HTTP_GET, getSettings);
    server.on("/api/settings", HTTP_POST, postSettings);
    server.on("/api/read", HTTP_POST, [](AsyncWebServerRequest* request) {
        app.readNow.store(true);
        sendOk(request);
    });
    server.on("/api/send", HTTP_POST, [](AsyncWebServerRequest* request) {
        app.sendNow.store(true);
        sendOk(request);
    });
    server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* request) {
        app.rebootNow.store(true);
        sendOk(request);
    });

    server.on("/api/log", HTTP_GET, getLog);
    wifi_portal::registerRoutes(server);

    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("no-cache");
    server.onNotFound([](AsyncWebServerRequest* request) {
        // Клиент точки доступа открыл чужой адрес — ведём в настройку Wi-Fi
        if (ON_AP_FILTER(request)) request->redirect("http://192.168.4.1/wifi.html");
        else request->send(404, "text/plain", "Not found");
    });
    server.begin();
}

void loop() {}

}  // namespace web
