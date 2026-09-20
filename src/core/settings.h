// Ядро: модель настроек устройства. Хранение — port/storage.*.
#pragma once
#include <stdint.h>

#include "opto_port.h"

namespace core {

// Менять при любом изменении структуры: старые настройки из NVS тогда
// заменятся умолчаниями, без миграции.
const uint16_t SETTINGS_VERSION = 4;

struct Settings {
    uint16_t version = SETTINGS_VERSION;

    // Wi-Fi
    char ssid[33] = "";
    char pass[65] = "";
    uint8_t bssid[6] = {0};  // быстрый коннект, как в waterius
    uint8_t channel = 0;     // 0 — канал неизвестен, полный скан

    // Статический адрес; 0 в ip — DHCP. Нужен там, где DHCP или DNS роутера
    // подводят: «в сети, но в облако не ходит» чаще всего именно про это.
    uint32_t ip = 0;
    uint32_t gateway = 0;
    uint32_t mask = 0;
    uint32_t dns = 0;  // 0 — шлюз; запасной сервер прошивка подставляет сама

    // Нет связи столько минут — перезагрузка (ESPHome reboot_timeout, 15 мин).
    // 0 — не перезагружаться. Пока кто-то настраивает через точку доступа,
    // перезагрузка не срабатывает.
    uint16_t rebootMin = 60;

    // Оптопорт
    SerialCfg serial;  // умолчания 9600 8N1 — параметры НАРТИС

    // Счётчик
    bool meterEnabled = true;   // тумблер «в работе»
    uint8_t meterAddr = 0;      // 0 = перебор 16 → 17
    char meterPwd[17] = "111";  // пароль LLS чтения, клиент 32 (заводской у серии 100)

    // Облако Waterius
    uint16_t periodMin = 60;
    char host[64] = "https://iz.waterius.ru";
    char key[41] = "";
    char email[64] = "";

    // Прозрачный serial
    bool rfcEnabled = true;
    uint16_t rfcPort = 2217;
};

// Эти поля на ходу не применяются: сервер RFC 2217 не перезапускается, а адрес
// интерфейса выставляется только при подключении к сети.
inline bool needsRestart(const Settings& a, const Settings& b) {
    return a.rfcEnabled != b.rfcEnabled || a.rfcPort != b.rfcPort || a.ip != b.ip ||
           a.gateway != b.gateway || a.mask != b.mask || a.dns != b.dns;
}

}  // namespace core
