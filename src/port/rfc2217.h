// Порт: прозрачный serial по RFC 2217 (telnet + COM-PORT-OPTION).
// Пока клиент подключён, оптопорт принадлежит ему, опрос счётчика не идёт.
#pragma once
#include <stdint.h>

namespace rfc2217 {
void begin(uint16_t port);
void loop();
bool busy();
}  // namespace rfc2217
