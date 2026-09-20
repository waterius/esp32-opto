#include "cloud.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace core {
namespace {

bool parseImage(JsonVariantConst v, OtaImage& img) {
    if (v.isNull()) return true;  // секции может не быть
    const char* url = v["url"];
    const char* md5 = v["md5"];
    if (!url || !md5 || !url[0] || strlen(url) >= sizeof(img.url) || strlen(md5) != 32) return false;
    snprintf(img.url, sizeof(img.url), "%s", url);
    snprintf(img.md5, sizeof(img.md5), "%s", md5);
    img.present = true;
    return true;
}

}  // namespace

size_t buildCloudPayload(const MeterData& m, uint32_t readAt, const Settings& s,
                         const DeviceInfo& dev, char* out, size_t cap) {
    JsonDocument doc;

    doc["key"] = s.key;
    doc["email"] = s.email;
    doc["sn"] = m.serial;

    // Показание и его код идут парой: что человек не выбрал, того в запросе
    // нет совсем. Иначе облако получило бы нули под видом показаний.
    if (s.totalType != DT_NONE) {
        doc["total"] = m.total;
        doc["data_type"] = (int)s.totalType;
    }
    for (uint8_t i = 0; i < m.tariffCount && i < MAX_TARIFFS; i++) {
        if (s.tariffType[i] == DT_NONE) continue;
        // Номер поля — номер тарифа в счётчике, без перенумерации: так строка
        // в логе сходится с показаниями на странице статуса.
        char name[12];
        snprintf(name, sizeof(name), "total%u", i + 1);
        doc[name] = m.tariff[i];
        snprintf(name, sizeof(name), "data_type%u", i + 1);
        doc[name] = (int)s.tariffType[i];
    }

    doc["fw"] = dev.fw;
    doc["model"] = m.model;
    // meter_fw, meter_time и meter_read_at в IZSerializer не описаны: лишние
    // поля бэкенд игнорирует, но данные счётчика лучше отправлять как есть.
    doc["meter_fw"] = m.fwVersion;
    doc["meter_time"] = m.time;
    char readAtIso[24] = "";
    if (readAt) {
        time_t t = (time_t)readAt;
        struct tm tm;
        gmtime_r(&t, &tm);
        strftime(readAtIso, sizeof(readAtIso), "%Y-%m-%dT%H:%M:%SZ", &tm);
    }
    doc["meter_read_at"] = readAtIso;
    doc["ota_error"] = dev.otaError;
    doc["reset_reason"] = dev.resetReason;
    doc["uptime"] = dev.uptimeS;
    doc["wifi_drops"] = dev.wifiDisconnects;
    doc["chip_id"] = dev.chipId;
    doc["ip"] = dev.ip;
    doc["rssi"] = dev.rssi;

    size_t n = serializeJson(doc, out, cap);
    return (n > 0 && n < cap) ? n : 0;
}

OtaParse parseOta(const char* body, OtaRequest& out) {
    out = OtaRequest();
    if (!body || !body[0]) return OtaParse::None;

    JsonDocument doc;
    if (deserializeJson(doc, body)) return OtaParse::None;  // не JSON — значит и ota нет

    JsonVariantConst ota = doc["ota"];
    if (ota.isNull()) return OtaParse::None;
    if (!ota.is<JsonObjectConst>()) return OtaParse::Error;

    if (!parseImage(ota["firmware"], out.firmware) || !parseImage(ota["filesystem"], out.filesystem))
        return OtaParse::Error;
    if (!out.firmware.present && !out.filesystem.present) return OtaParse::Error;
    return OtaParse::Ok;
}

}  // namespace core
