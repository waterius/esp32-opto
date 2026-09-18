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

    // Последнее успешное чтение счётчика (хранится в NVS)
    core::MeterData last;
    uint32_t lastReadAt = 0;  // UTC epoch; 0 — время было неизвестно
    bool hasReading = false;

    std::atomic<bool> meterReading{false};
    char meterError[64] = "";  // пусто — последнее чтение без ошибок

    uint32_t cloudAt = 0;       // UTC epoch последней успешной отправки
    int cloudCode = 0;          // HTTP-код последней попытки; <0 — нет соединения; 0 — не отправляли
    char cloudError[48] = "";   // пусто — последняя отправка успешна
    uint8_t otaError = 0;       // код ошибки OTA через сервер, уходит полем ota_error

    // Запросы с веб-страниц: поток async_tcp → loop()
    std::atomic<bool> readNow{false};
    std::atomic<bool> sendNow{false};
    std::atomic<bool> rebootNow{false};
    std::atomic<bool> settingsPending{false};
    core::Settings pendingSettings;
};

extern AppState app;
