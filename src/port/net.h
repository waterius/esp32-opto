// Порт: Wi-Fi-супервизор и HTTPS-клиент к облаку.
// Решения (когда переподключаться, когда поднимать точку доступа, когда
// перезагружаться) принимает core::WifiPolicy; здесь — только исполнение
// и сбор фактов. Всё вызывается из loop(), кроме колбэка событий SDK.
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

// Связь есть и адрес получен. Проверяются оба признака: WiFi.status() умеет
// застревать в WL_CONNECTED на мёртвом соединении (espressif/arduino-esp32#12714),
// поэтому к нему добавлено событие GOT_IP / DISCONNECTED от SDK.
bool connected();
bool apActive();
Status status();
const char* error();  // причина отказа для страницы /wifi; пусто — отказа не было
const char* modeName();  // "STA", "AP", "AP+STA"
const char* apName();    // esp32-opto-XXXX — имя точки доступа и hostname
String ip();
int rssi();
uint32_t chipId();

// true один раз после каждого нового подключения: канал и BSSID роутера
// для быстрого коннекта (как в waterius).
bool takeFastConnect(uint8_t& channel, uint8_t bssid[6]);

// Политика решила, что сохранённая пара канал+BSSID больше не находит сеть:
// её надо стереть из настроек, иначе быстрый коннект промахивается вечно.
bool takeForgetFastConnect();

// Сети нет столько, что помогает только перезагрузка.
bool rebootRequested();

// Диагностика для страницы статуса и лога.
uint16_t lastDisconnectReason();
uint32_t disconnectCount();
uint32_t offlineSeconds();

// Один TLS-клиент на всю прошивку: куча mbedTLS на C3 плохо переносит фрагментацию.
WiFiClientSecure& tlsClient();

// POST JSON на host из настроек + path. HTTP-код или <0 при ошибке соединения.
int postJson(const core::Settings& s, const char* path, const char* body, String& response);

}  // namespace net
