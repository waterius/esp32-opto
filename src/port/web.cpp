#include "web.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <LittleFS.h>

#include <memory>

#include "../app.h"
#include "../core/text.h"
#include "../poller.h"
#include "log.h"
#include "net.h"
#include "rfc2217.h"
#include "storage.h"
#include "watchdog.h"
#include "wifi_portal.h"

namespace web {
namespace {

AsyncWebServer server(80);

const long BAUDS[] = {300, 600, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};

// Лог отдаётся кусками. Раньше на каждый запрос выделялись все 16 КБ буфера, да
// ещё столько же уходило на сборку JSON — и так раз в секунду, в задаче
// async_tcp с приоритетом выше loop(). Это ровно та фрагментация кучи, из-за
// которой на C3 может не собраться TLS-сессия к облаку. Страница дочитает
// остаток следующим запросом: позиция next для этого и есть.
const size_t LOG_CHUNK = 4096;

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
    doc["safe_mode"] = app.safeMode;
    doc["wifi_drops"] = net::disconnectCount();
    doc["wifi_offline_s"] = net::offlineSeconds();
    doc["link_alive"] = net::linkAlive();
    doc["link_armed"] = net::linkGuardArmed();
    doc["boot_reason"] = app.bootReason;
    doc["watchdogs"] = watchdog::loopWatchdogArmed() && watchdog::rtcWatchdogArmed();

    doc["meter_enabled"] = app.sett.meterEnabled;
    doc["meter_reading"] = app.meterReading.load();
    doc["meter_error"] = app.meterError;
    doc["transparent"] = rfc2217::active();
    doc["transparent_idle_s"] = rfc2217::idleSeconds();

    core::MeterData last;
    uint32_t readAt = 0;
    bool hasReading = readReading(last, readAt);
    doc["has_reading"] = hasReading;
    doc["read_at"] = readAt;
    doc["serial"] = last.serial;
    doc["model"] = last.model;
    doc["meter_fw"] = last.fwVersion;
    doc["meter_time"] = last.time;
    doc["total"] = last.total;
    JsonArray tariffs = doc["tariffs"].to<JsonArray>();
    for (uint8_t i = 0; i < last.tariffCount && i < core::MAX_TARIFFS; ++i) tariffs.add(last.tariff[i]);
    // Кодов data_type здесь нет: страница показаний показывает то, что отдал
    // счётчик, — T1…T4. Чем тариф считать, видно там, где это выбирают, —
    // на странице настроек, где показание стоит над своим списком.

    doc["cloud_at"] = app.cloudAt;
    doc["cloud_code"] = app.cloudCode;
    doc["cloud_error"] = app.cloudError;
    doc["cloud_next_s"] = poller::secondsToNextSend();
    sendJson(request, doc);
}

// Пустая строка вместо 0.0.0.0: на странице это означает «адрес по DHCP».
String ipText(uint32_t addr) { return addr ? IPAddress(addr).toString() : String(); }

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
    doc["data_type"] = (int)s.totalType;
    for (uint8_t i = 0; i < core::MAX_TARIFFS; ++i) {
        char name[12];
        snprintf(name, sizeof(name), "data_type%u", i + 1);
        doc[name] = (int)s.tariffType[i];
    }
    doc["rfc_enabled"] = s.rfcEnabled;
    doc["rfc_port"] = s.rfcPort;
    doc["reboot_min"] = s.rebootMin;
    doc["ip"] = ipText(s.ip);
    doc["gateway"] = ipText(s.gateway);
    doc["mask"] = ipText(s.mask);
    doc["dns"] = ipText(s.dns);
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

// Адрес IPv4 или пусто. Пусто — 0, то есть DHCP.
bool paramIp(AsyncWebServerRequest* request, const char* name, uint32_t& out, JsonObject errors) {
    String v = param(request, name);
    if (v.isEmpty()) {
        out = 0;
        return true;
    }
    IPAddress addr;
    if (!addr.fromString(v)) {
        errors[name] = "Адрес вида 192.168.1.10 или пусто";
        return false;
    }
    out = (uint32_t)addr;
    return true;
}

// Чекбокс: formSubmit шлёт 1 или 0, как в портале waterius.
bool paramBool(AsyncWebServerRequest* request, const char* name) { return param(request, name) == "1"; }

// Код data_type из формы: DT_NONE или один из кодов Waterius. Диапазоном не
// проверить — коды не сплошные, поэтому решает ядро.
void paramDataType(AsyncWebServerRequest* request, const char* name, int8_t& out,
                   JsonObject errors) {
    long v = 0;
    const char* message = "Выберите тип данных из списка";
    if (!paramLong(request, name, core::DT_NONE, core::DT_HALF_PEAK, v, errors, message)) return;
    if (!core::validDataType(v)) errors[name] = message;
    else out = (int8_t)v;
}

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
    paramDataType(request, "data_type", s.totalType, errors);
    for (uint8_t i = 0; i < core::MAX_TARIFFS; ++i) {
        char name[12];
        snprintf(name, sizeof(name), "data_type%u", i + 1);
        paramDataType(request, name, s.tariffType[i], errors);
    }

    s.rfcEnabled = paramBool(request, "rfc_enabled");
    if (paramLong(request, "rfc_port", 1, 65535, v, errors, "Порт: от 1 до 65535")) s.rfcPort = (uint16_t)v;

    if (paramLong(request, "reboot_min", 0, 1440, v, errors, "От 0 до 1440 минут; 0 — не перезагружаться"))
        s.rebootMin = (uint16_t)v;
    bool ipOk = paramIp(request, "ip", s.ip, errors);
    ipOk = paramIp(request, "gateway", s.gateway, errors) && ipOk;
    ipOk = paramIp(request, "mask", s.mask, errors) && ipOk;
    ipOk = paramIp(request, "dns", s.dns, errors) && ipOk;
    // Половина статики хуже, чем её отсутствие: без шлюза и маски интерфейс не поднимется
    if (ipOk && s.ip && (!s.gateway || !s.mask))
        errors["ip"] = "Со статическим адресом нужны шлюз и маска";

    if (errors.size()) {
        sendJson(request, doc);
        return;
    }

    bool reboot = core::needsRestart(s, app.sett);
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
    std::unique_ptr<char[]> text(new (std::nothrow) char[LOG_CHUNK + 1]);
    if (!text) {
        request->send(503, "text/plain", "не хватило памяти");
        return;
    }
    size_t len = 0;
    bool skipped = false;
    uint32_t next = Log.read(from, text.get(), LOG_CHUNK, len, skipped);
    // Кусок мог кончиться посреди буквы: отдаём до её начала, остаток придёт
    // следующим запросом — иначе страница получит битый UTF-8
    size_t kept = core::utf8Trim(text.get(), len);
    next -= (uint32_t)(len - kept);
    text[kept] = 0;

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
    if (!LittleFS.begin()) Log.error("LittleFS не смонтирован: залейте образ командой uploadfs\n");

    // /api/status и /api/log окно портала НЕ продлевают: страницы опрашивают их
    // сами раз в секунду, и открытая вкладка держала бы устройство в точке
    // доступа вечно. Всё остальное — действия человека.
    server.on("/api/status", HTTP_GET, getStatus);
    server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest* request) {
        net::feedPortal();
        getSettings(request);
    });
    server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest* request) {
        net::feedPortal();
        postSettings(request);
    });
    server.on("/api/read", HTTP_POST, [](AsyncWebServerRequest* request) {
        net::feedPortal();
        app.readNow.store(true);
        sendOk(request);
    });
    server.on("/api/send", HTTP_POST, [](AsyncWebServerRequest* request) {
        net::feedPortal();
        app.sendNow.store(true);
        sendOk(request);
    });
    server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* request) {
        app.restartReason.store((uint8_t)core::RestartReason::WebButton);
        app.rebootNow.store(true);
        sendOk(request);
    });

    server.on("/api/log", HTTP_GET, getLog);
    wifi_portal::registerRoutes(server);
    ElegantOTA.begin(&server);  // страница /update: прошивка и образ LittleFS
    // Колбэк работает в задаче async_tcp, поэтому здесь только атомарный флаг;
    // в NVS его переносит loop(), см. web::loop()
    ElegantOTA.onEnd([](bool success) {
        if (success) app.restartReason.store((uint8_t)core::RestartReason::OtaWeb);
    });

    server.serveStatic("/", LittleFS, "/")
        .setDefaultFile("index.html")
        .setCacheControl("no-cache")
        .setFilter([](AsyncWebServerRequest*) {
            net::feedPortal();  // человек открыл страницу — портал нужен ему дальше
            return true;
        });
    server.onNotFound([](AsyncWebServerRequest* request) {
        // Клиент точки доступа открыл чужой адрес — ведём в настройку Wi-Fi
        if (ON_AP_FILTER(request)) request->redirect("http://192.168.4.1/wifi.html");
        else request->send(404, "text/plain", "Not found");
    });
    server.begin();
}

void loop() {
    // ElegantOTA перезагружает плату сама, изнутри своего loop(): причину надо
    // успеть записать до этого вызова, иначе она останется безликой
    // «программной перезагрузкой»
    if (app.restartReason.load() == (uint8_t)core::RestartReason::OtaWeb) {
        storage::saveRestartReason(core::RestartReason::OtaWeb);
        app.restartReason.store((uint8_t)core::RestartReason::Unknown);
    }
    ElegantOTA.loop();
}

}  // namespace web
