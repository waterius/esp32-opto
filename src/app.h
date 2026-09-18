// Общее состояние прошивки. Пишет loop(); веб-хендлеры читают его без блокировок
// (это только отображение) и выставляют атомарные флаги запросов.
#pragma once
#include <atomic>

#include "core/meter.h"
#include "core/settings.h"

struct AppState {
    core::Settings sett;

    // Слишком много загрузок подряд не дожили до пяти минут: работают только
    // сеть, страницы и обновление. Ставится в setup(), дальше не меняется.
    bool safeMode = false;

    // Последнее успешное чтение счётчика (хранится в NVS).
    // Пишет только poller из loop(), читают веб-хендлеры из задачи async_tcp —
    // структура большая, и без счётчика версий страница может показать половину
    // прошлых показаний и половину новых (тарифы — double, на 32 битах рвётся
    // пополам). Писать через AppState::WriteReading, читать через readReading().
    core::MeterData last;
    uint32_t lastReadAt = 0;  // UTC epoch; 0 — время было неизвестно
    bool hasReading = false;
    std::atomic<uint32_t> readingSeq{0};  // нечётное — запись идёт прямо сейчас

    // Только из loop(): поднимает версию до и после записи показаний.
    struct WriteReading {
        AppState& app;
        explicit WriteReading(AppState& a) : app(a) { app.readingSeq.fetch_add(1); }
        ~WriteReading() { app.readingSeq.fetch_add(1); }
    };

    std::atomic<bool> meterReading{false};
    char meterError[core::METER_ERROR_CAP] = "";  // пусто — последнее чтение без ошибок

    uint32_t cloudAt = 0;       // UTC epoch последней успешной отправки
    int cloudCode = 0;          // HTTP-код последней попытки; <0 — нет соединения; 0 — не отправляли
    char cloudError[64] = "";   // пусто — последняя отправка успешна
    uint8_t otaError = 0;       // код ошибки OTA через сервер, уходит полем ota_error

    // Запросы с веб-страниц: поток async_tcp → loop()
    std::atomic<bool> readNow{false};
    std::atomic<bool> sendNow{false};
    std::atomic<bool> rebootNow{false};
    std::atomic<bool> settingsPending{false};
    core::Settings pendingSettings;
};

extern AppState app;

// Снимок показаний для веб-хендлера: повторяем, если loop() писал в этот момент.
// Восьми попыток заведомо хватает — запись занимает доли микросекунды.
inline bool readReading(core::MeterData& out, uint32_t& readAt) {
    for (int attempt = 0; attempt < 8; ++attempt) {
        uint32_t before = app.readingSeq.load();
        if (before & 1) continue;
        out = app.last;
        readAt = app.lastReadAt;
        bool has = app.hasReading;
        if (app.readingSeq.load() == before) return has;
    }
    out = app.last;
    readAt = app.lastReadAt;
    return app.hasReading;
}
