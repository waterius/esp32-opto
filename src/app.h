// Общее состояние прошивки. Заполняется в main.cpp.
#pragma once
#include "core/meter.h"
#include "settings.h"

extern const char* FIRMWARE_VERSION;

struct AppState {
    Settings sett;
    core::MeterData last;      // последнее чтение счётчика
    char cloudStatus[64] = "";  // результат последней отправки
    uint32_t lastReadMs = 0;
    bool readNow = false;       // запрос чтения с веб-страницы
};

extern AppState app;

// Применить к оптопорту параметры из настроек (после прозрачной сессии тоже).
void applySerialCfg();
