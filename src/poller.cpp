#include "poller.h"

#include <Arduino.h>
#include <time.h>

#include "app.h"
#include "core/cloud.h"
#include "core/nartis.h"
#include "port/log.h"
#include "port/net.h"
#include "port/opto_bus.h"
#include "port/storage.h"

namespace poller {
namespace {

const uint32_t RETRY_MS = 5UL * 60 * 1000;
// Первый цикл вскоре после старта, чтобы после перепрошивки не ждать целый период.
const uint32_t FIRST_CYCLE_MS = 30UL * 1000;

core::NartisMeter meter(bus);

uint32_t periodStartMs = 0;
bool readRetry = false;
uint32_t readRetryStartMs = 0;
bool sendRetry = false;
uint32_t sendRetryStartMs = 0;

uint32_t periodMs() { return (uint32_t)app.sett.periodMin * 60000UL; }

// UTC epoch или 0, пока NTP не синхронизировался.
uint32_t epochNow() {
    time_t t = time(nullptr);
    return t > 1600000000 ? (uint32_t)t : 0;
}

void readMeter() {
    readRetry = false;
    if (!app.sett.meterEnabled) return;
    if (!bus.acquireForMeter()) return;  // порт у прозрачной сессии

    meter.setAddress(app.sett.meterAddr);
    meter.setPassword(app.sett.meterPwd);
    bus.configure(app.sett.serial);

    app.meterReading.store(true);
    core::MeterData data;
    char error[sizeof(app.meterError)];
    core::ReadResult result = meter.read(data, error, sizeof(error));
    app.meterReading.store(false);
    bus.releaseMeter();

    switch (result) {
        case core::ReadResult::Ok:
            app.last = data;
            app.lastReadAt = epochNow();
            app.hasReading = true;
            app.meterError[0] = 0;
            storage::saveLastReading(app.last, app.lastReadAt);
            Log.printf("Счётчик: всего %.3f кВт·ч, тарифов %u, sn %s\n", data.total, data.tariffCount,
                          data.serial);
            break;
        case core::ReadResult::Failed:
            snprintf(app.meterError, sizeof(app.meterError), "%s", error);
            readRetry = true;
            readRetryStartMs = millis();
            Log.printf("Счётчик: %s, повтор через 5 минут\n", error);
            break;
        case core::ReadResult::AuthRejected:
            // После 5 неверных паролей счётчик блокируется на сутки — опрос выключаем
            snprintf(app.meterError, sizeof(app.meterError), "%s", error);
            app.sett.meterEnabled = false;
            storage::saveSettings(app.sett);
            Log.printf("Счётчик: %s, опрос выключен\n", error);
            break;
        case core::ReadResult::Aborted:
            snprintf(app.meterError, sizeof(app.meterError), "чтение прервано прозрачной сессией");
            Log.println("Счётчик: чтение прервано прозрачной сессией");
            break;
    }
}

void sendCloud() {
    sendRetry = false;
    if (!app.hasReading) {
        snprintf(app.cloudError, sizeof(app.cloudError), "нет данных счётчика");
        return;
    }
    if (!app.sett.key[0]) {
        snprintf(app.cloudError, sizeof(app.cloudError), "ключ облака не задан");
        return;
    }

    char body[768];
    String ip = net::ip();
    core::DeviceInfo dev;
    dev.fw = FIRMWARE_VERSION;
    dev.ip = ip.c_str();
    dev.rssi = net::rssi();
    dev.chipId = net::chipId();
    dev.otaError = app.otaError;
    if (!core::buildCloudPayload(app.last, app.lastReadAt, app.sett, dev, body, sizeof(body))) {
        snprintf(app.cloudError, sizeof(app.cloudError), "запрос не собрался");
        return;
    }

    String response;
    int code = net::postJson(app.sett, "/api/source/iz/", body, response);
    app.cloudCode = code;
    Log.printf("Облако: HTTP %d %s\n", code, response.c_str());
    if (code != 200) {
        snprintf(app.cloudError, sizeof(app.cloudError), code < 0 ? "нет соединения" : "сервер ответил ошибкой");
        sendRetry = true;
        sendRetryStartMs = millis();
        return;
    }

    app.cloudAt = epochNow();
    app.cloudError[0] = 0;
    if (app.otaError) {  // ошибка OTA доставлена — обнуляем
        app.otaError = 0;
        storage::saveOtaError(0);
    }
}

}  // namespace

void begin() {
    // Беззнаковая арифметика: now - periodStartMs = periodMs - FIRST_CYCLE_MS
    periodStartMs = millis() - periodMs() + FIRST_CYCLE_MS;
}

void loop() {
    uint32_t now = millis();

    if (app.readNow.exchange(false)) {
        readMeter();
        return;
    }
    // Выход на связь — всегда чтение, затем отправка последних прочитанных данных,
    // даже если чтение не удалось или опрос выключен
    if (app.sendNow.exchange(false)) {
        readMeter();
        sendCloud();
        return;
    }
    if (now - periodStartMs >= periodMs()) {
        periodStartMs = now;
        readMeter();
        sendCloud();
        return;
    }
    if (sendRetry && now - sendRetryStartMs >= RETRY_MS) {
        sendCloud();
        return;
    }
    if (readRetry && now - readRetryStartMs >= RETRY_MS) readMeter();
}

void onMeterEnabled() {
    readRetry = false;
    app.meterError[0] = 0;
}

uint32_t secondsToNextSend() {
    uint32_t elapsed = millis() - periodStartMs;
    uint32_t period = periodMs();
    return elapsed >= period ? 0 : (period - elapsed) / 1000;
}

}  // namespace poller
