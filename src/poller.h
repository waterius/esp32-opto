// Автомат опроса счётчика и отправки в облако. Работает только из loop().
#pragma once
#include <stdint.h>

namespace poller {

void begin();
void loop();

// Пользователь снова включил опрос тумблером «в работе».
void onMeterEnabled();

// Через сколько секунд очередная отправка по периоду.
uint32_t secondsToNextSend();

}  // namespace poller
