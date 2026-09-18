// Ядро: данные счётчика и интерфейс адаптера.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "opto_port.h"

namespace core {

const uint8_t MAX_TARIFFS = 4;

struct MeterData {
    double total = 0;                  // суммарный расход, кВт·ч
    double tariff[MAX_TARIFFS] = {0};  // T1..T4, кВт·ч
    uint8_t tariffCount = 0;           // сколько тарифов прочитано
    char serial[20] = {0};             // серийный номер счётчика
    char model[40] = {0};              // тип счётчика, UTF-8
    char fwVersion[20] = {0};          // версия ПО счётчика
    char time[24] = {0};               // время счётчика, "ГГГГ-ММ-ДД ЧЧ:ММ:СС"
};

enum class ReadResult {
    Ok,
    Failed,        // нет ответа или ошибка протокола — повтор через 5 минут
    AuthRejected,  // счётчик отверг пароль — опрос выключается
    Aborted,       // порт забрала прозрачная сессия
};

class IMeter {
   public:
    virtual ~IMeter() {}
    // Полный цикл: связь, чтение, разрыв. error — текст для страницы статуса.
    virtual ReadResult read(MeterData& out, char* error, size_t errorCap) = 0;
};

}  // namespace core
