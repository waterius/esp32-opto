#include "poller.h"

#include <Arduino.h>
#include <time.h>

#include "app.h"
#include "core/cloud.h"
#include "core/nartis.h"
#include "core/text.h"
#include "core/schedule.h"
#include "port/log.h"
#include "port/net.h"
#include "port/opto_bus.h"
#include "port/ota_cloud.h"
#include "port/rfc2217.h"
#include "port/storage.h"
#include "port/watchdog.h"

namespace poller {
namespace {

const uint32_t READ_RETRY_MS = 5UL * 60 * 1000;
// Повтор отправки: 5 минут, потом 10, 20, 40 — но не реже часа. Сутки лежащего
// сервера стоили 288 бесполезных TLS-рукопожатий подряд.
const uint32_t SEND_RETRY_BASE_MS = 5UL * 60 * 1000;
const uint32_t SEND_RETRY_MAX_MS = 60UL * 60 * 1000;
// Верхняя граница разброса периода; сам разброс — десятая часть периода.
const uint32_t JITTER_MAX_MS = 5UL * 60 * 1000;
// Первый цикл вскоре после старта, чтобы после перепрошивки не ждать целый период.
const uint32_t FIRST_CYCLE_MS = 30UL * 1000;

core::NartisMeter meter(bus);

uint32_t periodStartMs = 0;
uint32_t cycleMs = 0;          // период плюс разброс: длина текущего цикла
uint16_t cyclePeriodMin = 0;   // из каких настроек посчитан cycleMs
bool readRetry = false;
uint32_t readRetryStartMs = 0;
bool sendRetry = false;
uint32_t sendRetryStartMs = 0;
uint32_t sendRetryDelayMs = 0;
uint8_t sendFails = 0;

uint32_t periodMs() { return (uint32_t)app.sett.periodMin * 60000UL; }

// Период плюс постоянный для устройства сдвиг. Без него все устройства с этой
// прошивкой выходят на связь в одну и ту же секунду.
uint32_t cycleLength() {
    uint32_t period = periodMs();
    uint32_t span = period / 10;
    if (span > JITTER_MAX_MS) span = JITTER_MAX_MS;
    cyclePeriodMin = app.sett.periodMin;
    return period + core::jitterMs(net::chipId(), span);
}

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
        case core::ReadResult::Ok: {
            uint32_t readAt = epochNow();
            {
                // Счётчик версий поднят ровно на время записи: запись в NVS и
                // строка в лог заняли бы миллисекунды, и всё это время страница
                // крутилась бы в повторах чтения
                AppState::WriteReading writing(app);
                app.last = data;
                app.lastReadAt = readAt;
                app.hasReading = true;
            }
            app.meterError[0] = 0;
            storage::saveLastReading(data, readAt);
            Log.printf("Счётчик: всего %.3f кВт·ч, тарифов %u, sn %s\n", data.total, data.tariffCount,
                          data.serial);
            break;
        }
        case core::ReadResult::Failed:
            snprintf(app.meterError, sizeof(app.meterError), "%s", error);
            core::utf8Truncate(app.meterError);
            readRetry = true;
            readRetryStartMs = millis();
            Log.warn("Счётчик: %s, повтор через 5 минут\n", error);
            break;
        case core::ReadResult::AuthRejected:
            // После 5 неверных паролей счётчик блокируется на сутки — опрос выключаем
            snprintf(app.meterError, sizeof(app.meterError), "%s", error);
            core::utf8Truncate(app.meterError);
            app.sett.meterEnabled = false;
            storage::saveSettings(app.sett);
            Log.error("Счётчик: %s, опрос выключен\n", error);
            break;
        case core::ReadResult::Aborted:
            snprintf(app.meterError, sizeof(app.meterError), "чтение прервано прозрачной сессией");
            Log.println("Счётчик: чтение прервано прозрачной сессией");
            break;
    }
}

// Блок ota в ответе облака — как в waterius main.cpp после send_data.
void handleOta(const String& response) {
    core::OtaRequest req;
    core::OtaParse parsed = core::parseOta(response.c_str(), req);
    if (parsed == core::OtaParse::None) return;
    if (parsed == core::OtaParse::Error) {
        app.otaError = core::OTA_ERR_PARSE;
        storage::saveOtaError(app.otaError);
        return;
    }
    if (rfc2217::active()) {
        Log.println("OTA: пропущено, идёт прозрачная сессия — сервер пришлёт блок снова");
        return;
    }
    app.otaError = ota_cloud::run(req);  // при успехе перезагрузка и сюда не вернёмся
    storage::saveOtaError(app.otaError);
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

    char body[896];
    String ip = net::ip();
    core::DeviceInfo dev;
    dev.fw = FIRMWARE_VERSION;
    dev.ip = ip.c_str();
    dev.rssi = net::rssi();
    dev.chipId = net::chipId();
    dev.otaError = app.otaError;
    dev.resetReason = watchdog::resetReasonCode();
    dev.uptimeS = millis() / 1000;
    dev.wifiDisconnects = net::disconnectCount();
    if (!core::buildCloudPayload(app.last, app.lastReadAt, app.sett, dev, body, sizeof(body))) {
        snprintf(app.cloudError, sizeof(app.cloudError), "запрос не собрался");
        return;
    }

    String response;
    int code = net::postJson(app.sett, "/api/source/iz/", body, response);
    app.cloudCode = code;
    // Ответ сервера кладём в лог обрезанным: целиком это сотни байт JSON,
    // которые никому не нужны, зато выталкивают из буфера страницы всё полезное
    Log.info("Облако: HTTP %d %.120s\n", code, response.c_str());
    if (code != 200) {
        snprintf(app.cloudError, sizeof(app.cloudError), code < 0 ? "нет соединения" : "сервер ответил ошибкой");
        if (sendFails < 255) ++sendFails;
        sendRetryDelayMs = core::retryDelayMs(sendFails, SEND_RETRY_BASE_MS, SEND_RETRY_MAX_MS);
        sendRetry = true;
        sendRetryStartMs = millis();
        Log.warn("Облако: повтор через %lu минут\n", (unsigned long)(sendRetryDelayMs / 60000UL));
        return;
    }

    sendFails = 0;
    app.cloudAt = epochNow();
    app.cloudError[0] = 0;
    if (app.otaError) {  // ошибка OTA доставлена — обнуляем
        app.otaError = 0;
        storage::saveOtaError(0);
    }
    handleOta(response);
}

}  // namespace

void begin() {
    cycleMs = cycleLength();
    // Беззнаковая арифметика: now - periodStartMs = cycleMs - FIRST_CYCLE_MS
    periodStartMs = millis() - cycleMs + FIRST_CYCLE_MS;
}

void loop() {
    uint32_t now = millis();
    if (app.sett.periodMin != cyclePeriodMin) cycleMs = cycleLength();  // период сменили на странице

    if (app.readNow.exchange(false)) {
        readMeter();
        return;
    }
    // sendCloud() — блокирующий HTTPS-запрос до 12 секунд; пока он идёт, loop()
    // не вызывает rfc2217::loop(), и байты прозрачной сессии теряются. Поэтому,
    // как и с OTA, на время сессии цикл просто откладывается: periodStartMs и
    // таймеры повтора не трогаем, sendNow/readRetry/sendRetry остаются взведены,
    // и после закрытия сессии цикл сработает сам, при необходимости — сразу.
    if (rfc2217::active()) return;

    // Выход на связь — всегда чтение, затем отправка последних прочитанных данных,
    // даже если чтение не удалось или опрос выключен
    if (app.sendNow.exchange(false)) {
        readMeter();
        sendCloud();
        return;
    }
    if (now - periodStartMs >= cycleMs) {
        periodStartMs = now;
        cycleMs = cycleLength();
        readMeter();
        sendCloud();
        return;
    }
    if (sendRetry && now - sendRetryStartMs >= sendRetryDelayMs) {
        sendCloud();
        return;
    }
    if (readRetry && now - readRetryStartMs >= READ_RETRY_MS) readMeter();
}

void onMeterEnabled() {
    readRetry = false;
    app.meterError[0] = 0;
}

uint32_t secondsToNextSend() {
    uint32_t elapsed = millis() - periodStartMs;
    return elapsed >= cycleMs ? 0 : (cycleMs - elapsed) / 1000;
}

}  // namespace poller
