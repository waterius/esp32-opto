// Ядро: обмен с облаком Waterius — тело запроса POST в корень хоста и разбор
// блока ota в ответе.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "meter.h"
#include "settings.h"

namespace core {

// Коды поля ota_error — как в waterius (ESP8266/src/core/types.h).
// Код 4 (батарея) не используется: питание постоянное.
enum OtaError : uint8_t {
    OTA_OK = 0,
    OTA_ERR_PARSE = 1,
    OTA_ERR_FS = 2,
    OTA_ERR_FIRMWARE = 3,
};

// То, что знает только порт.
struct DeviceInfo {
    const char* fw = "";  // версия этой прошивки
    const char* ip = "";
    int rssi = 0;
    uint32_t chipId = 0;
    uint8_t otaError = OTA_OK;

    // Диагностика: без неё «устройство перезагрузилось» неотличимо от дёрганого
    // питания, просадки и паники. Бэкенд эти поля пока игнорирует.
    uint8_t resetReason = 0;  // esp_reset_reason()
    uint32_t uptimeS = 0;
    uint32_t wifiDisconnects = 0;
};

// readAt — UTC epoch чтения, 0 если время было неизвестно.
// Возвращает длину JSON или 0, если не влезло в cap.
size_t buildCloudPayload(const MeterData& m, uint32_t readAt, const Settings& s,
                         const DeviceInfo& dev, char* out, size_t cap);

struct OtaImage {
    bool present = false;
    char url[256] = "";
    char md5[33] = "";
};

struct OtaRequest {
    OtaImage firmware;
    OtaImage filesystem;
};

enum class OtaParse { None, Ok, Error };

// Блок {"ota":{"firmware":{"url","md5","size"},"filesystem":{...}}} —
// формат waterius (ESP8266/src/ota_parse.h). None — блока нет.
OtaParse parseOta(const char* body, OtaRequest& out);

}  // namespace core
