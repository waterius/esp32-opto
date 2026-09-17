// esp32-opto: счётчик НАРТИС через оптопорт → облако Waterius, веб-морда,
// прозрачный serial по RFC 2217.
// Дизайн: docs/superpowers/specs/2026-09-17-esp32-opto-firmware-design.md
#include <Arduino.h>

#include "app.h"
#include "port/log.h"
#include "port/opto_bus.h"
#include "port/storage.h"

AppState app;

void setup() {
    Log.begin(115200);
    delay(200);
    Log.printf("esp32-opto %s\n", FIRMWARE_VERSION);

    storage::loadSettings(app.sett);
    app.hasReading = storage::loadLastReading(app.last, app.lastReadAt);
    app.otaError = storage::loadOtaError();

    bus.begin(app.sett.serial);
    Log.printf("Настройки: оптопорт %lu %u%c%u, сеть «%s», период %u мин, чтение в NVS: %s\n",
                  (unsigned long)app.sett.serial.baud, app.sett.serial.bits, app.sett.serial.parity,
                  app.sett.serial.stop, app.sett.ssid, app.sett.periodMin, app.hasReading ? "есть" : "нет");
}

void loop() { delay(100); }
