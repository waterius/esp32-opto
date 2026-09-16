// Порт: время для ядра.
#include <Arduino.h>

#include "../core/opto_port.h"

namespace core {
uint32_t nowMs() { return millis(); }
void sleepMs(uint32_t ms) { delay(ms); }
}  // namespace core
