// Порт: время для ядра.
#include <Arduino.h>

#include "../core/opto_port.h"
#include "watchdog.h"

namespace core {
uint32_t nowMs() { return millis(); }

// Кормим сторож цикла: обмен со счётчиком ждёт кадры именно здесь и при
// переборе адресов может занять больше минуты, оставаясь исправным.
void sleepMs(uint32_t ms) {
    watchdog::feed();
    delay(ms);
}
}  // namespace core
