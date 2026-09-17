// Ядро: что мы читаем со счётчика и интерфейс адаптера.
#pragma once
#include <stdint.h>

#include "opto_port.h"  // SerialCfg

namespace core {

const uint8_t MAX_TARIFFS = 4;

struct MeterData {
    bool valid = false;
    double total = 0;                  // суммарный расход, кВт·ч
    double tariff[MAX_TARIFFS] = {0};  // T1..T4, кВт·ч
    uint8_t tariffCount = 0;           // сколько тарифов реально прочитано
    char serial[20] = {0};             // серийный номер счётчика
    char fwVersion[20] = {0};          // версия ПО счётчика
    char time[24] = {0};               // время счётчика, "ГГГГ-ММ-ДД ЧЧ:ММ:СС"
    char model[32] = {0};              // тип счётчика, UTF-8
    char error[48] = {0};              // текст ошибки, если valid == false
};

class IMeter {
   public:
    virtual ~IMeter() {}
    // Полный цикл: связь, чтение всех параметров, разрыв связи.
    virtual bool read(MeterData& out) = 0;
    // Параметры порта, которые нужны этому счётчику.
    virtual SerialCfg defaultSerial() const = 0;
};

}  // namespace core
