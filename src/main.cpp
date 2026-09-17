// esp32-opto: счётчик НАРТИС через оптопорт → облако Waterius, веб-морда,
// прозрачный serial по RFC 2217.
// Дизайн: docs/superpowers/specs/2026-09-17-esp32-opto-firmware-design.md
#include <Arduino.h>

#include "app.h"
#include "core/nartis.h"
#include "port/log.h"
#include "port/opto_bus.h"
#include "port/storage.h"

AppState app;

namespace {

// Временная проверка адаптера: чтение раз в минуту в лог. В задаче 4 её заменит poller.
core::NartisMeter testMeter(bus);
const uint32_t TEST_READ_EVERY_MS = 60UL * 1000;
uint32_t testReadAt = 0;
bool testStopped = false;

void testRead() {
    if (testStopped || (testReadAt && millis() - testReadAt < TEST_READ_EVERY_MS)) return;
    testReadAt = millis() | 1;
    if (!bus.acquireForMeter()) return;

    testMeter.setAddress(app.sett.meterAddr);
    testMeter.setPassword(app.sett.meterPwd);
    bus.configure(app.sett.serial);
    core::MeterData data;
    char error[64];
    core::ReadResult result = testMeter.read(data, error, sizeof(error));
    bus.releaseMeter();

    Log.printf("Чтение: результат %d %s\n", (int)result, error);
    if (result == core::ReadResult::AuthRejected) {
        // Повторять нельзя: 5 неверных паролей — блокировка счётчика на сутки
        testStopped = true;
        Log.println("Опрос остановлен до перезагрузки");
        return;
    }
    if (result != core::ReadResult::Ok) return;
    Log.printf("  всего %.3f кВт·ч, sn %s, модель %s, ПО %s, время %s\n", data.total, data.serial,
                  data.model, data.fwVersion, data.time);
    for (uint8_t i = 0; i < data.tariffCount; ++i) Log.printf("  T%u %.3f кВт·ч\n", i + 1, data.tariff[i]);
}

}  // namespace

void setup() {
    Log.begin(115200);
    delay(200);
    Log.printf("esp32-opto %s\n", FIRMWARE_VERSION);

    storage::loadSettings(app.sett);
    app.hasReading = storage::loadLastReading(app.last, app.lastReadAt);
    app.otaError = storage::loadOtaError();

    bus.begin(app.sett.serial);
}

void loop() {
    testRead();
    delay(100);
}
