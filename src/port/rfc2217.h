// Порт: прозрачный serial по RFC 2217 поверх igrr/rfc2217-server (lib/rfc2217-server).
// Сервер работает в своих задачах; его колбэки только кладут байты в буфер и
// ставят флаги. С UART работает loop() через OptoBus.
#pragma once
#include <stdint.h>

namespace rfc2217 {
void begin(uint16_t port);
void loop();
bool active();  // клиент подключён и оптопорт у него

// Сколько секунд в открытой сессии нет байт: видно на странице статуса, чтобы
// «почему молчит облако» не приходилось выяснять вслепую.
uint32_t idleSeconds();
}  // namespace rfc2217
