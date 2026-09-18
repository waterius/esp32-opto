// esp32-opto: счётчик НАРТИС через оптопорт → облако Waterius, веб-морда,
// прозрачный serial по RFC 2217.
// Дизайн: docs/superpowers/specs/2026-09-17-esp32-opto-firmware-design.md
#include <Arduino.h>
#include <ArduinoOTA.h>

#include "app.h"
#include "poller.h"
#include "port/log.h"
#include "port/net.h"
#include "port/opto_bus.h"
#include "port/rfc2217.h"
#include "port/storage.h"
#include "port/web.h"
#include "port/wifi_portal.h"

#ifndef BOOT_PIN
#define BOOT_PIN 0
#endif

AppState app;

namespace {

const uint32_t FACTORY_RESET_HOLD_MS = 5000;

// Настройки со страницы /settings. В NVS пишет только loop().
void applyPendingSettings() {
    if (!app.settingsPending.exchange(false)) return;
    core::Settings next = app.pendingSettings;
    // Сеть меняется только со страницы /wifi — не затираем её копией из формы
    memcpy(next.ssid, app.sett.ssid, sizeof(next.ssid));
    memcpy(next.pass, app.sett.pass, sizeof(next.pass));
    memcpy(next.bssid, app.sett.bssid, sizeof(next.bssid));
    next.channel = app.sett.channel;

    bool meterTurnedOn = next.meterEnabled && !app.sett.meterEnabled;
    bool meterTurnedOff = !next.meterEnabled && app.sett.meterEnabled;
    bool reboot = next.rfcEnabled != app.sett.rfcEnabled || next.rfcPort != app.sett.rfcPort;
    app.sett = next;
    storage::saveSettings(app.sett);
    Log.println("Настройки сохранены");
    if (meterTurnedOn) poller::onMeterEnabled();
    // Иначе после ручного выключения тумблера страница показывает старую причину
    // ошибки (например, отказ пароля), хотя опрос выключен пользователем, не счётчиком.
    if (meterTurnedOff) app.meterError[0] = 0;
    if (reboot) app.rebootNow.store(true);  // сервер RFC 2217 на ходу не перезапускается
}

// Канал и BSSID роутера после подключения — для быстрого коннекта (как в waterius).
void saveFastConnect() {
    uint8_t channel = 0;
    uint8_t bssid[6];
    if (!net::takeFastConnect(channel, bssid)) return;
    if (channel == app.sett.channel && memcmp(bssid, app.sett.bssid, sizeof(bssid)) == 0) return;
    app.sett.channel = channel;
    memcpy(app.sett.bssid, bssid, sizeof(bssid));
    storage::saveSettings(app.sett);
}

// ArduinoOTA — для pio run -t upload --upload-port <IP>. Стартует, когда появилась сеть.
void arduinoOta() {
    static bool started = false;
    if (!started) {
        if (!net::connected()) return;
        ArduinoOTA.setHostname(net::apName());
        ArduinoOTA.setMdnsEnabled(false);
        ArduinoOTA.begin();
        started = true;
    }
    ArduinoOTA.handle();
}

// Удержание BOOT 5 секунд — сброс к заводским настройкам.
void checkFactoryReset() {
    static uint32_t pressedAt = 0;
    if (digitalRead(BOOT_PIN) == HIGH) {
        pressedAt = 0;
        return;
    }
    if (pressedAt == 0) {
        pressedAt = millis() | 1;
        return;
    }
    if (millis() - pressedAt < FACTORY_RESET_HOLD_MS) return;
    Log.println("Сброс к заводским настройкам");
    storage::resetAll();
    delay(300);
    ESP.restart();
}

}  // namespace

void setup() {
    Log.begin(115200);
    delay(200);
    Log.printf("esp32-opto %s\n", FIRMWARE_VERSION);
    pinMode(BOOT_PIN, INPUT_PULLUP);

    storage::loadSettings(app.sett);
    app.hasReading = storage::loadLastReading(app.last, app.lastReadAt);
    app.otaError = storage::loadOtaError();

    bus.begin(app.sett.serial);
    net::begin(app.sett);
    web::begin();
    if (app.sett.rfcEnabled) rfc2217::begin(app.sett.rfcPort);
    poller::begin();
}

void loop() {
    net::loop(app.sett);
    saveFastConnect();
    wifi_portal::loop();
    web::loop();
    arduinoOta();
    rfc2217::loop();
    applyPendingSettings();
    poller::loop();
    checkFactoryReset();
    if (app.rebootNow.load()) {
        delay(300);
        ESP.restart();
    }
    delay(2);
}
