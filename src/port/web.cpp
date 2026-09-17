#include "web.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include <memory>

#include "log.h"
#include "wifi_portal.h"

namespace web {
namespace {

AsyncWebServer server(80);

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
