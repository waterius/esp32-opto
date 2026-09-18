// Порт: Wi-Fi-супервизор и HTTPS-клиент к облаку.
#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>

#include "../core/settings.h"

namespace net {

enum class Status : uint8_t { Idle, Connecting, Connected, Failed };

void begin(const core::Settings& s);
void loop(const core::Settings& s);
// Сеть сменили со страницы /wifi.
void reconnect(const core::Settings& s);

bool connected();
bool apActive();
Status status();
const char* modeName();  // "STA", "AP", "AP+STA"
const char* apName();    // esp32-opto-XXXX — имя точки доступа и hostname
String ip();
int rssi();
uint32_t chipId();

// true один раз после каждого нового подключения: канал и BSSID роутера
// для быстрого коннекта (как в waterius).
bool takeFastConnect(uint8_t& channel, uint8_t bssid[6]);

// Один TLS-клиент на всю прошивку: куча mbedTLS на C3 плохо переносит фрагментацию.
WiFiClientSecure& tlsClient();

// POST JSON на host из настроек + path. HTTP-код или <0 при ошибке соединения.
int postJson(const core::Settings& s, const char* path, const char* body, String& response);

}  // namespace net
