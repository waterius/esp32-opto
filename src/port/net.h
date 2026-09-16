// Порт: Wi-Fi и HTTP.
#pragma once
#include <Arduino.h>

#include "../settings.h"

namespace net {

void begin(const Settings& s);
void loop();
bool connected();
bool isAp();
String ip();
int rssi();
uint32_t chipId();

// POST JSON. Возвращает HTTP-код или отрицательное число при ошибке.
int postJson(const Settings& s, const char* path, const char* body, String& response);

}  // namespace net
