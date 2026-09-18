// Ядро: модель настроек устройства. Хранение — port/storage.*.
#pragma once
#include <stdint.h>

#include "opto_port.h"

namespace core {

// Менять при любом изменении структуры: старые настройки из NVS тогда
// заменятся умолчаниями, без миграции.
const uint16_t SETTINGS_VERSION = 2;

struct Settings {
    uint16_t version = SETTINGS_VERSION;

    // Wi-Fi
    char ssid[33] = "";
    char pass[65] = "";
    uint8_t bssid[6] = {0};  // быстрый коннект, как в waterius
    uint8_t channel = 0;     // 0 — канал неизвестен, полный скан

    // Оптопорт
    SerialCfg serial;  // умолчания 9600 8N1 — параметры НАРТИС

    // Счётчик
    bool meterEnabled = true;   // тумблер «в работе»
    uint8_t meterAddr = 0;      // 0 = перебор 16 → 17
    char meterPwd[17] = "111";  // пароль LLS чтения, клиент 32 (заводской у серии 100)

    // Облако Waterius
    uint16_t periodMin = 60;
    char host[64] = "https://cloud.waterius.ru";
    char key[41] = "";
    char email[64] = "";

    // Прозрачный serial
    bool rfcEnabled = true;
    uint16_t rfcPort = 2217;
};

}  // namespace core
