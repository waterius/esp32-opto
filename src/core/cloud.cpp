#include "cloud.h"

#include <ArduinoJson.h>

#include "../settings.h"

namespace core {
namespace {

// Коды типов данных Waterius (apps/source/api/api.py в waterius.site.back)
const int DT_ELECTRICITY = 2;  // сумма
const int DT_DAY = 5;          // T1
const int DT_NIGHT = 6;        // T2
const int DT_PEAK = 7;         // T3
const int DT_HALF_PEAK = 8;    // T4

int tariffDataType(uint8_t idx) {
    switch (idx) {
        case 0: return DT_DAY;
        case 1: return DT_NIGHT;
        case 2: return DT_PEAK;
        default: return DT_HALF_PEAK;
    }
}

}  // namespace

size_t buildCloudPayload(const MeterData& m, const Settings& s, const DeviceInfo& dev, char* out,
                         size_t cap) {
    JsonDocument doc;

    doc["key"] = s.key;
    doc["email"] = s.email;
    doc["sn"] = m.serial;
    doc["total"] = m.total;
    doc["data_type"] = DT_ELECTRICITY;

    for (uint8_t i = 0; i < m.tariffCount; i++) {
        char name[12];
        snprintf(name, sizeof(name), "total%u", i + 1);
        doc[name] = m.tariff[i];
        snprintf(name, sizeof(name), "data_type%u", i + 1);
        doc[name] = tariffDataType(i);
    }

    doc["fw"] = dev.fw;
    doc["model"] = m.model;
    // meter_fw и meter_time в IZSerializer не описаны: лишние поля бэкенд
    // игнорирует, но данные счётчика лучше отправлять как есть.
    doc["meter_fw"] = m.fwVersion;
    doc["meter_time"] = m.time;
    doc["chip_id"] = dev.chipId;
    doc["ip"] = dev.ip;
    doc["rssi"] = dev.rssi;

    size_t n = serializeJson(doc, out, cap);
    return (n > 0 && n < cap) ? n : 0;
}

}  // namespace core
