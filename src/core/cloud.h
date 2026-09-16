// Ядро: сборка тела запроса для облака Waterius (POST /api/source/iz/).
#pragma once
#include <stddef.h>

#include "meter.h"

struct Settings;

namespace core {

// Дополнительные поля, которые знает только порт.
struct DeviceInfo {
    const char* fw = "";      // версия этой прошивки
    const char* ip = "";
    int rssi = 0;
    uint32_t chipId = 0;
};

// Возвращает длину JSON или 0, если не влезло.
size_t buildCloudPayload(const MeterData& m, const Settings& s, const DeviceInfo& dev, char* out,
                         size_t cap);

}  // namespace core
