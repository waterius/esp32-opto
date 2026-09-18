#include "core_clock.h"

#include "core/opto_port.h"

namespace testclock {
uint32_t ms = 0;
}  // namespace testclock

namespace core {
// nowMs() двигает часы сам: иначе цикл ожидания кадра в nartis.cpp крутился бы
// на месте вечно, ведь ждать в тестах нечего.
uint32_t nowMs() { return ++testclock::ms; }
void sleepMs(uint32_t ms) { testclock::ms += ms; }
}  // namespace core
