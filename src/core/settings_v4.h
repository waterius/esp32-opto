// Ядро: замороженная побайтно раскладка блоба настроек версии 4 — того, что
// прошивки до перехода на JSON клали в NVS ключом `settings`.
//
// Файл нужен ровно для одного: один раз прочитать старую запись на плате,
// которая обновляется с прошлой прошивки, и переложить её в новый формат
// (port/storage.cpp). **Когда на платах не останется записи `settings`, файл
// удаляется целиком.**
//
// Поля объявлены самостоятельными — без SerialCfg и MAX_TARIFFS. Иначе правка
// SerialCfg или числа тарифов молча «разморозит» раскладку и перенос начнёт
// читать чужие байты. Выравнивание задано явными полями-заполнителями, а
// static_assert ниже сверяет каждое смещение: раскладка обязана совпадать и на
// xtensa, и на riscv32, и на x86-64 хоста, где гоняются тесты.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "settings.h"

namespace core {

struct SettingsV4 {
    uint16_t version;
    char ssid[33];
    char pass[65];
    uint8_t bssid[6];
    uint8_t channel;
    uint8_t pad0;
    uint32_t ip;
    uint32_t gateway;
    uint32_t mask;
    uint32_t dns;
    uint16_t rebootMin;
    uint8_t pad1[2];
    uint32_t baud;  // SerialCfg развёрнут в поля
    uint8_t bits;
    char parity;
    uint8_t stop;
    uint8_t pad2;  // хвостовой заполнитель SerialCfg: выравнивание на 4
    uint8_t meterEnabled;  // bool в исходной структуре; на всех целях это 1 байт
    uint8_t meterAddr;
    char meterPwd[17];
    uint8_t pad3;
    uint16_t periodMin;
    char host[64];
    char key[41];
    char email[64];
    int8_t totalType;
    int8_t tariffType[4];  // MAX_TARIFFS на момент версии 4
    uint8_t rfcEnabled;
    uint8_t pad4;
    uint16_t rfcPort;
};

static_assert(sizeof(SettingsV4) == 336, "раскладка v4 изменилась");
static_assert(offsetof(SettingsV4, version) == 0, "раскладка v4 изменилась");
static_assert(offsetof(SettingsV4, ssid) == 2, "раскладка v4 изменилась");
static_assert(offsetof(SettingsV4, pass) == 35, "раскладка v4 изменилась");
static_assert(offsetof(SettingsV4, bssid) == 100, "раскладка v4 изменилась");
static_assert(offsetof(SettingsV4, channel) == 106, "раскладка v4 изменилась");
static_assert(offsetof(SettingsV4, ip) == 108, "раскладка v4 изменилась");
static_assert(offsetof(SettingsV4, gateway) == 112, "раскладка v4 изменилась");
static_assert(offsetof(SettingsV4, mask) == 116, "раскладка v4 изменилась");
static_assert(offsetof(SettingsV4, dns) == 120, "раскладка v4 изменилась");
static_assert(offsetof(SettingsV4, rebootMin) == 124, "раскладка v4 изменилась");
static_assert(offsetof(SettingsV4, baud) == 128, "раскладка v4 изменилась");
static_assert(offsetof(SettingsV4, bits) == 132, "раскладка v4 изменилась");
static_assert(offsetof(SettingsV4, parity) == 133, "раскладка v4 изменилась");
static_assert(offsetof(SettingsV4, stop) == 134, "раскладка v4 изменилась");
static_assert(offsetof(SettingsV4, meterEnabled) == 136, "раскладка v4 изменилась");
static_assert(offsetof(SettingsV4, meterAddr) == 137, "раскладка v4 изменилась");
static_assert(offsetof(SettingsV4, meterPwd) == 138, "раскладка v4 изменилась");
static_assert(offsetof(SettingsV4, periodMin) == 156, "раскладка v4 изменилась");
static_assert(offsetof(SettingsV4, host) == 158, "раскладка v4 изменилась");
static_assert(offsetof(SettingsV4, key) == 222, "раскладка v4 изменилась");
static_assert(offsetof(SettingsV4, email) == 263, "раскладка v4 изменилась");
static_assert(offsetof(SettingsV4, totalType) == 327, "раскладка v4 изменилась");
static_assert(offsetof(SettingsV4, tariffType) == 328, "раскладка v4 изменилась");
static_assert(offsetof(SettingsV4, rfcEnabled) == 332, "раскладка v4 изменилась");
static_assert(offsetof(SettingsV4, rfcPort) == 334, "раскладка v4 изменилась");

namespace v4detail {
// В исправной записи последний байт поля и так нуль, но запись могла побиться:
// строка без нуля утащила бы за собой соседние поля.
inline void copyStr(char* dst, size_t dstCap, const char* src, size_t srcCap) {
    size_t n = srcCap < dstCap ? srcCap : dstCap;
    memcpy(dst, src, n);
    dst[n - 1] = 0;
}
}  // namespace v4detail

inline void fromV4(const SettingsV4& v, Settings& s) {
    s = Settings();  // поля, которых в версии 4 не было, берут умолчание
    v4detail::copyStr(s.ssid, sizeof(s.ssid), v.ssid, sizeof(v.ssid));
    v4detail::copyStr(s.pass, sizeof(s.pass), v.pass, sizeof(v.pass));
    memcpy(s.bssid, v.bssid, sizeof(s.bssid));
    s.channel = v.channel;
    s.ip = v.ip;
    s.gateway = v.gateway;
    s.mask = v.mask;
    s.dns = v.dns;
    s.rebootMin = v.rebootMin;
    s.serial.baud = v.baud;
    s.serial.bits = v.bits;
    s.serial.parity = v.parity;
    s.serial.stop = v.stop;
    s.meterEnabled = v.meterEnabled != 0;
    s.meterAddr = v.meterAddr;
    v4detail::copyStr(s.meterPwd, sizeof(s.meterPwd), v.meterPwd, sizeof(v.meterPwd));
    s.periodMin = v.periodMin;
    v4detail::copyStr(s.host, sizeof(s.host), v.host, sizeof(v.host));
    v4detail::copyStr(s.key, sizeof(s.key), v.key, sizeof(v.key));
    v4detail::copyStr(s.email, sizeof(s.email), v.email, sizeof(v.email));
    s.totalType = v.totalType;
    for (uint8_t i = 0; i < MAX_TARIFFS && i < 4; ++i) s.tariffType[i] = v.tariffType[i];
    s.rfcEnabled = v.rfcEnabled != 0;
    s.rfcPort = v.rfcPort;
}

}  // namespace core
