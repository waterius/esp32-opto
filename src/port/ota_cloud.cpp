#include "ota_cloud.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Update.h>

#include "log.h"
#include "net.h"

namespace ota_cloud {
namespace {

const uint32_t DOWNLOAD_TIMEOUT_MS = 15000;

// command: U_FLASH — прошивка, U_SPIFFS — раздел ФС (subtype spiffs, в нём LittleFS).
bool flash(const core::OtaImage& img, int command) {
    Log.printf("OTA: %s %s\n", command == U_SPIFFS ? "ФС" : "прошивка", img.url);

    HTTPClient http;
    http.setTimeout(DOWNLOAD_TIMEOUT_MS);
    WiFiClient plain;
    bool https = strncmp(img.url, "https://", 8) == 0;
    if (!(https ? http.begin(net::tlsClient(), img.url) : http.begin(plain, img.url))) return false;

    int code = http.GET();
    int len = http.getSize();
    if (code != 200 || len <= 0) {
        Log.printf("OTA: HTTP %d, размер %d\n", code, len);
        http.end();
        return false;
    }
    if (!Update.begin((size_t)len, command)) {
        Log.printf("OTA: %s\n", Update.errorString());
        http.end();
        return false;
    }
    if (!Update.setMD5(img.md5)) {
        Log.printf("OTA: контрольная сумма не принята: %s\n", img.md5);
        Update.abort();
        http.end();
        return false;
    }
    size_t written = Update.writeStream(*http.getStreamPtr());
    bool ok = written == (size_t)len && Update.end();
    if (!ok) {
        Log.printf("OTA: записано %u из %d, %s\n", (unsigned)written, len, Update.errorString());
        Update.abort();
    }
    http.end();
    return ok;
}

}  // namespace

uint8_t run(const core::OtaRequest& req) {
    if (req.filesystem.present && !flash(req.filesystem, U_SPIFFS)) return core::OTA_ERR_FS;
    if (req.firmware.present && !flash(req.firmware, U_FLASH)) return core::OTA_ERR_FIRMWARE;
    Log.println("OTA: готово, перезагрузка");
    delay(300);
    ESP.restart();
    return core::OTA_OK;
}

}  // namespace ota_cloud
