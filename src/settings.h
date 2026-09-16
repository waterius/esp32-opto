// Настройки устройства. Модель — общая, хранение — в порту (port/settings_nvs.cpp).
#pragma once
#include "core/opto_port.h"

struct Settings {
    // Wi-Fi
    char ssid[33] = "";
    char pass[65] = "";
    // Оптопорт
    core::SerialCfg serial;  // по умолчанию 9600 8N1 — как у НАРТИС
    // Счётчик
    uint8_t meterAddr = 0;      // 0 = автоопределение (16, затем 17)
    char meterPwd[17] = "111";  // пароль LLS чтения, клиент 32 (завод, серия 100)
    // Облако Waterius
    uint16_t periodMin = 60;
    char host[64] = "https://cloud.waterius.ru";
    char key[41] = "";
    char email[64] = "";
    // Прозрачный serial
    uint16_t rfcPort = 2217;
};

void settingsLoad(Settings& s);
void settingsSave(const Settings& s);
