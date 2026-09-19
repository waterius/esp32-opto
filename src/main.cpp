// esp32-opto: счётчик НАРТИС через оптопорт → облако Waterius, веб-морда,
// прозрачный serial по RFC 2217.
// Дизайн: docs/superpowers/specs/2026-09-17-esp32-opto-firmware-design.md
#include <Arduino.h>
#include <ArduinoOTA.h>

#include "app.h"
#include "core/boot_guard.h"
#include "core/restart_reason.h"
#include "poller.h"
#include "port/log.h"
#include "port/net.h"
#include "port/opto_bus.h"
#include "port/rfc2217.h"
#include "port/storage.h"
#include "port/watchdog.h"
#include "port/web.h"
#include "port/wifi_portal.h"

#ifndef BOOT_PIN
#define BOOT_PIN 0
#endif

AppState app;

namespace {

const uint32_t FACTORY_RESET_HOLD_MS = 5000;

core::BootGuard bootGuard;

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
    bool reboot = core::needsRestart(next, app.sett);
    app.sett = next;
    storage::saveSettings(app.sett);
    Log.println("Настройки сохранены");
    if (meterTurnedOn) poller::onMeterEnabled();
    // Иначе после ручного выключения тумблера страница показывает старую причину
    // ошибки (например, отказ пароля), хотя опрос выключен пользователем, не счётчиком.
    if (meterTurnedOff) app.meterError[0] = 0;
    if (reboot) {  // см. core::needsRestart
        app.restartReason.store((uint8_t)core::RestartReason::Settings);
        app.rebootNow.store(true);
    }
}

// Канал и BSSID роутера после подключения — для быстрого коннекта (как в waterius).
// Обратная сторона: если роутер переехал, политика просит пару забыть, иначе
// быстрый коннект будет промахиваться вечно.
void syncFastConnect() {
    if (net::takeForgetFastConnect()) {
        Log.println("Wi-Fi: сохранённые канал и BSSID больше не находят сеть — забываем");
        app.sett.channel = 0;
        memset(app.sett.bssid, 0, sizeof(app.sett.bssid));
        storage::saveFastConnect(app.sett);
        return;
    }
    uint8_t channel = 0;
    uint8_t bssid[6];
    if (!net::takeFastConnect(channel, bssid)) return;
    if (channel == app.sett.channel && memcmp(bssid, app.sett.bssid, sizeof(bssid)) == 0) return;
    app.sett.channel = channel;
    memcpy(app.sett.bssid, bssid, sizeof(bssid));
    storage::saveFastConnect(app.sett);
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
    storage::saveRestartReason(core::RestartReason::FactoryReset);  // после очистки, иначе сотрётся
    delay(300);
    ESP.restart();
}

}  // namespace

void setup() {
    Log.begin(115200);
    delay(200);
    watchdog::begin();
    Log.printf("esp32-opto %s\n", FIRMWARE_VERSION);
    // Без этой строки «устройство перезагрузилось» неотличимо от дёрганого
    // питания, просадки, паники и нашей же плановой перезагрузки
    core::RestartReason planned = storage::loadRestartReason();
    app.bootReason = core::restartText(planned, watchdog::trippedLastBoot(), watchdog::resetReason());
    Log.printf("Причина загрузки: %s\n", app.bootReason);
    if (planned != core::RestartReason::Unknown)
        storage::saveRestartReason(core::RestartReason::Unknown);  // причина учтена
    pinMode(BOOT_PIN, INPUT_PULLUP);

    storage::saveBootCount(bootGuard.onBoot(storage::loadBootCount()));
    app.safeMode = bootGuard.safeMode();

    storage::loadSettings(app.sett);
    app.hasReading = storage::loadLastReading(app.last, app.lastReadAt);
    app.otaError = storage::loadOtaError();

    if (app.safeMode) {
        Log.printf("УСЕЧЁННЫЙ РЕЖИМ: %u загрузок подряд не дожили до пяти минут\n",
                   bootGuard.bootCount());
        Log.println("Опрос счётчика и прозрачный serial выключены. Залейте прошивку на /update");
    }

    bus.begin(app.sett.serial);
    net::setSafeMode(app.safeMode);
    net::begin(app.sett);
    web::begin();
    if (app.sett.rfcEnabled && !app.safeMode) rfc2217::begin(app.sett.rfcPort);
    if (!app.safeMode) poller::begin();
}

void loop() {
    net::loop(app.sett);
    syncFastConnect();
    wifi_portal::loop();
    web::loop();
    arduinoOta();
    rfc2217::loop();  // в усечённом режиме сервер не запущен и вернётся сразу
    applyPendingSettings();
    if (!app.safeMode) poller::loop();
    checkFactoryReset();

    // Продержались достаточно долго — загрузка засчитана, счётчик обнуляется.
    // Плановые перезагрузки (смена настроек, OTA, час без сети) случаются уже
    // после этого и в счётчик не попадают.
    if (bootGuard.takeBootIsGood(millis())) {
        storage::saveBootCount(0);
        Log.println("Загрузка признана удачной");
    }

    if (net::rebootRequested()) {
        app.restartReason.store((uint8_t)core::RestartReason::NoNetwork);
        app.rebootNow.store(true);
    }
    if (app.rebootNow.load()) {
        storage::saveRestartReason((core::RestartReason)app.restartReason.load());
        delay(300);
        ESP.restart();
    }
    watchdog::feed();
    delay(2);
}
