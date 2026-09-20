#include "settings_io.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

namespace core {
namespace {

void tariffKey(char* out, size_t cap, uint8_t i) { snprintf(out, cap, "data_type%u", i + 1); }

void toHex(const uint8_t* src, size_t len, char* out) {
    static const char DIGITS[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        out[i * 2] = DIGITS[src[i] >> 4];
        out[i * 2 + 1] = DIGITS[src[i] & 0x0f];
    }
    out[len * 2] = 0;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Только целый BSSID: половина адреса хуже, чем его отсутствие — по неполному
// быстрый коннект уйдёт в никуда, а полный скан уже не начнётся.
bool fromHex(const char* s, uint8_t* dst, size_t len) {
    if (!s || strlen(s) != len * 2) return false;
    uint8_t tmp[16];
    if (len > sizeof(tmp)) return false;
    for (size_t i = 0; i < len; ++i) {
        int hi = hexDigit(s[i * 2]);
        int lo = hexDigit(s[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        tmp[i] = (uint8_t)((hi << 4) | lo);
    }
    memcpy(dst, tmp, len);
    return true;
}

// Ключа нет или значение не строка — поле остаётся с умолчанием. Длинная
// строка обрезается по размеру буфера поля.
void readStr(JsonObjectConst o, const char* key, char* dst, size_t cap) {
    const char* v = o[key];
    if (v) snprintf(dst, cap, "%s", v);
}

// Значение вне диапазона поле не трогает: так запись из будущего, где у поля
// другие единицы, не превращается в мусор.
template <typename T>
void readNum(JsonObjectConst o, const char* key, T& dst, long long lo, long long hi) {
    JsonVariantConst v = o[key];
    if (!v.is<long long>()) return;
    long long x = v.as<long long>();
    if (x < lo || x > hi) return;
    dst = (T)x;
}

void readBool(JsonObjectConst o, const char* key, bool& dst) {
    JsonVariantConst v = o[key];
    if (v.is<bool>()) dst = v.as<bool>();
}

void writeDoc(const Settings& s, JsonDocument& doc) {
    doc["v"] = SETTINGS_VERSION;

    doc["ssid"] = s.ssid;
    doc["pass"] = s.pass;
    char bssid[13];
    toHex(s.bssid, sizeof(s.bssid), bssid);
    doc["bssid"] = bssid;
    doc["channel"] = s.channel;

    // Адреса — числами, как они лежат в структуре: разбирать точечную запись
    // ядру нечем (IPAddress — это Arduino), а запись в NVS человек не читает.
    doc["ip"] = s.ip;
    doc["gateway"] = s.gateway;
    doc["mask"] = s.mask;
    doc["dns"] = s.dns;
    doc["reboot_min"] = s.rebootMin;

    doc["baud"] = s.serial.baud;
    doc["bits"] = s.serial.bits;
    char parity[2] = {s.serial.parity, 0};
    doc["parity"] = parity;
    doc["stop"] = s.serial.stop;

    doc["meter_enabled"] = s.meterEnabled;
    doc["meter_addr"] = s.meterAddr;
    doc["meter_pwd"] = s.meterPwd;

    doc["period_min"] = s.periodMin;
    doc["host"] = s.host;
    doc["key"] = s.key;
    doc["email"] = s.email;

    doc["data_type"] = (int)s.totalType;
    for (uint8_t i = 0; i < MAX_TARIFFS; ++i) {
        char name[12];
        tariffKey(name, sizeof(name), i);
        doc[name] = (int)s.tariffType[i];
    }

    doc["rfc_enabled"] = s.rfcEnabled;
    doc["rfc_port"] = s.rfcPort;
}

// Поле version в структуре не читается: в памяти версия всегда текущая, а в
// записи за неё отвечает ключ "v".
void readDoc(JsonObjectConst o, Settings& s) {
    readStr(o, "ssid", s.ssid, sizeof(s.ssid));
    readStr(o, "pass", s.pass, sizeof(s.pass));
    fromHex(o["bssid"], s.bssid, sizeof(s.bssid));
    readNum(o, "channel", s.channel, 0, 255);

    readNum(o, "ip", s.ip, 0, 0xFFFFFFFFLL);
    readNum(o, "gateway", s.gateway, 0, 0xFFFFFFFFLL);
    readNum(o, "mask", s.mask, 0, 0xFFFFFFFFLL);
    readNum(o, "dns", s.dns, 0, 0xFFFFFFFFLL);
    readNum(o, "reboot_min", s.rebootMin, 0, 65535);

    readNum(o, "baud", s.serial.baud, 1, 0xFFFFFFFFLL);
    readNum(o, "bits", s.serial.bits, 5, 8);
    const char* parity = o["parity"];
    if (parity && !parity[1] && (parity[0] == 'N' || parity[0] == 'E' || parity[0] == 'O'))
        s.serial.parity = parity[0];
    readNum(o, "stop", s.serial.stop, 1, 2);

    readBool(o, "meter_enabled", s.meterEnabled);
    readNum(o, "meter_addr", s.meterAddr, 0, 255);
    readStr(o, "meter_pwd", s.meterPwd, sizeof(s.meterPwd));

    readNum(o, "period_min", s.periodMin, 1, 65535);
    readStr(o, "host", s.host, sizeof(s.host));
    readStr(o, "key", s.key, sizeof(s.key));
    readStr(o, "email", s.email, sizeof(s.email));

    // Коды не сплошные, диапазоном не проверить — решает validDataType()
    JsonVariantConst total = o["data_type"];
    if (total.is<long long>() && validDataType((long)total.as<long long>()))
        s.totalType = (int8_t)total.as<long long>();
    for (uint8_t i = 0; i < MAX_TARIFFS; ++i) {
        char name[12];
        tariffKey(name, sizeof(name), i);
        JsonVariantConst v = o[name];
        if (v.is<long long>() && validDataType((long)v.as<long long>()))
            s.tariffType[i] = (int8_t)v.as<long long>();
    }

    readBool(o, "rfc_enabled", s.rfcEnabled);
    readNum(o, "rfc_port", s.rfcPort, 1, 65535);
}

// Апгрейды. Пишутся ТОЛЬКО на смену смысла уже существующего поля: единицы,
// интерпретация, имя, разделение на два. Новое поле берёт умолчание само,
// удалённое молча игнорируется — версию это не двигает.
//
// Цепочка идёт по документу, а не по разобранной структуре: переименование
// ключа иначе потеряло бы значение ещё до апгрейда.
//
// Так выглядела бы смена хоста облака по умолчанию из dcda09b, стоившая тогда
// всех настроек на уже настроенных платах:
//
//   void upgrade4to5(JsonObject o) {
//       const char* host = o["host"];
//       if (host && strcmp(host, "https://cloud.waterius.ru") == 0)
//           o["host"] = "https://iz.waterius.ru";
//   }
//
// false — запись из будущего (откат прошивки): цепочку не гоняем.
bool applyUpgrades(JsonObject o) {
    long long v = o["v"] | 0LL;
    if (v > SETTINGS_VERSION) return false;
    while (v < SETTINGS_VERSION) {
        switch (v) {
            // case 4: upgrade4to5(o); break;
            default: break;
        }
        ++v;
    }
    o["v"] = SETTINGS_VERSION;
    return true;
}

}  // namespace

size_t settingsToJson(const Settings& s, char* out, size_t cap) {
    if (!out || !cap) return 0;
    JsonDocument doc;
    writeDoc(s, doc);
    size_t n = serializeJson(doc, out, cap);
    return (n > 0 && n < cap) ? n : 0;
}

LoadResult settingsFromJson(const char* json, size_t len, Settings& s) {
    s = Settings();
    if (!json || !len) return LoadResult::Defaults;

    JsonDocument doc;
    if (deserializeJson(doc, json, len)) return LoadResult::Defaults;
    JsonObject o = doc.as<JsonObject>();
    if (o.isNull()) return LoadResult::Defaults;

    long long was = o["v"] | 0LL;
    if (!applyUpgrades(o)) {
        readDoc(o, s);
        return LoadResult::FromFuture;
    }
    readDoc(o, s);
    return was < SETTINGS_VERSION ? LoadResult::Migrated : LoadResult::Ok;
}

}  // namespace core
