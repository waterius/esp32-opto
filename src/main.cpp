// esp32-opto: счётчик НАРТИС по оптопорту → облако Waterius,
// плюс прозрачный serial по RFC 2217.
#include <Arduino.h>

#include "app.h"
#include "core/cloud.h"
#include "core/nartis.h"
#include "port/net.h"
#include "port/opto_esp32.h"
#include "port/rfc2217.h"
#include "port/web.h"

const char* FIRMWARE_VERSION = "0.1.0";

AppState app;
static core::NartisMeter meter(opto);

void applySerialCfg() { opto.configure(app.sett.serial); }

static void readAndSend() {
    app.lastReadMs = millis();

    meter.setAddress(app.sett.meterAddr);
    meter.setPassword(app.sett.meterPwd);
    applySerialCfg();

    if (!meter.read(app.last)) {
        Serial.printf("счётчик не прочитан: %s\n", app.last.error);
        snprintf(app.cloudStatus, sizeof(app.cloudStatus), "нет данных счётчика");
        return;
    }
    Serial.printf("всего %.3f кВт·ч, тарифов %u, sn %s\n", app.last.total, app.last.tariffCount,
                  app.last.serial);

    if (app.sett.key[0] == 0) {
        snprintf(app.cloudStatus, sizeof(app.cloudStatus), "ключ не задан");
        return;
    }
    core::DeviceInfo dev;
    String ip = net::ip();
    dev.fw = FIRMWARE_VERSION;
    dev.ip = ip.c_str();
    dev.rssi = net::rssi();
    dev.chipId = net::chipId();

    char body[512];
    if (!core::buildCloudPayload(app.last, app.sett, dev, body, sizeof(body))) {
        snprintf(app.cloudStatus, sizeof(app.cloudStatus), "не собрался запрос");
        return;
    }
    String resp;
    int code = net::postJson(app.sett, "/api/source/iz/", body, resp);
    snprintf(app.cloudStatus, sizeof(app.cloudStatus), "HTTP %d", code);
    Serial.printf("облако: %d %s\n", code, resp.c_str());
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\nesp32-opto %s\n", FIRMWARE_VERSION);

    settingsLoad(app.sett);
    opto.begin(app.sett.serial);
    net::begin(app.sett);
    web::begin();
    rfc2217::begin(app.sett.rfcPort);

    Serial.printf("IP %s, веб на :80, RFC 2217 на :%u\n", net::ip().c_str(), app.sett.rfcPort);
}

void loop() {
    net::loop();
    web::loop();
    rfc2217::loop();

    // Пока идёт прозрачная сессия, порт занят — счётчик не опрашиваем.
    if (rfc2217::busy()) return;

    uint32_t periodMs = (uint32_t)app.sett.periodMin * 60000UL;
    bool due = app.lastReadMs == 0 || millis() - app.lastReadMs >= periodMs;
    if (app.readNow || (due && net::connected())) {
        app.readNow = false;
        readAndSend();
    }
    delay(5);
}
