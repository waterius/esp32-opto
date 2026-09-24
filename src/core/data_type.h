// Ядро: коды data_type облака Waterius — чем считать показание, которое лежит
// в поле total / totalN.
//
// Счётчик не знает, какой его тариф дневной, а какой ночной: в нём это просто
// T1…T4. Соответствие назначает человек на странице настроек, поэтому код
// хранится в core::Settings, а не выводится из номера тарифа.
#pragma once
#include <stdint.h>

namespace core {

enum DataType : int8_t {
    DT_NONE = -1,        // не отправлять это показание совсем
    DT_ELECTRICITY = 2,  // электричество, всего
    DT_DAY = 5,
    DT_NIGHT = 6,
    DT_PEAK = 7,
    DT_HALF_PEAK = 8,
};

// Коды не сплошные (3 и 4 — вода и газ), поэтому диапазоном не проверить.
inline bool validDataType(long v) {
    return v == DT_NONE || v == DT_ELECTRICITY || v == DT_DAY || v == DT_NIGHT || v == DT_PEAK ||
           v == DT_HALF_PEAK;
}

}  // namespace core
