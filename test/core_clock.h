// Часы ядра для юнит-тестов: общий файл в корне test/, поэтому линкуется в
// каждый набор. Нужен всем, кто тянет src/core: настоящих задержек в тестах нет.
#pragma once
#include <stdint.h>

namespace testclock {

extern uint32_t ms;  // виртуальное время, миллисекунды

inline void reset(uint32_t start = 0) { ms = start; }

}  // namespace testclock
