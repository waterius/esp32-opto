#include "ota_cloud.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Update.h>

#include "../core/restart_reason.h"
#include "log.h"
#include "net.h"
#include "storage.h"
#include "watchdog.h"

namespace ota_cloud {
namespace {

const uint32_t DOWNLOAD_TIMEOUT_MS = 15000;

// Контекст колбэка прогресса. Update.onProgress принимает std::function, и
// лямбда с захватом ушла бы в кучу — а куча в этот момент занята TLS-сессией и
// буфером Update. Поэтому лямбда без захвата, а контекст лежит здесь.
const char* progressWhat = "";
uint8_t progressFifth = 0;

const char* imageName(int command) { return command == U_SPIFFS ? "ФС" : "прошивка"; }

// Секунды с десятыми, без %f: поддержка float в printf у newlib-nano
// необязательна, а ради одной строки лога её включать незачем.
void logDone(const char* what, unsigned written, uint32_t ms) {
    Log.printf("OTA: %s — успех, записано %u байт за %u,%u с, контрольная сумма сошлась\n", what,
               written, (unsigned)(ms / 1000), (unsigned)(ms % 1000 / 100));
}

// command: U_FLASH — прошивка, U_SPIFFS — раздел ФС (subtype spiffs, в нём LittleFS).
bool flash(const core::OtaImage& img, int command) {
    const char* what = imageName(command);
    const uint32_t startMs = millis();
    Log.printf("OTA: %s — начинаем, %s\n", what, img.url);

    // plain объявлен раньше http, чтобы уничтожался позже — иначе на ветке
    // раннего выхода деструктор HTTPClient трогал бы уже мёртвый WiFiClient.
    WiFiClient plain;
    HTTPClient http;
    http.setTimeout(DOWNLOAD_TIMEOUT_MS);
    bool https = strncmp(img.url, "https://", 8) == 0;
    if (!(https ? http.begin(net::tlsClient(), img.url) : http.begin(plain, img.url))) {
        Log.error("OTA: %s — ошибка, адрес не принят\n", what);
        return false;
    }

    int code = http.GET();
    int len = http.getSize();
    if (code != 200 || len <= 0) {
        Log.error("OTA: %s — ошибка, сервер отдал HTTP %d и размер %d\n", what, code, len);
        http.end();
        return false;
    }
    if (!Update.begin((size_t)len, command)) {
        Log.error("OTA: %s — ошибка, раздел не готов: %s\n", what, Update.errorString());
        http.end();
        return false;
    }
    if (!Update.setMD5(img.md5)) {
        Log.error("OTA: %s — ошибка, контрольная сумма не принята: %s\n", what, img.md5);
        Update.abort();
        http.end();
        return false;
    }
    Log.printf("OTA: %s — пишем %d байт, ждём md5 %s\n", what, len, img.md5);

    // Запись образа идёт минутами, а loop() в это время не крутится: кормим
    // сторож здесь, иначе он примет исправное обновление за зависание. Заодно
    // отмечаем пятые доли: без них между «пишем» и «успех» лог молчит секунд
    // двадцать, и со стороны не отличить работу от зависания.
    progressWhat = what;
    progressFifth = 0;
    Update.onProgress([](size_t done, size_t total) {
        watchdog::feed();
        if (!total) return;
        uint8_t fifth = (uint8_t)(done * 5 / total);
        if (fifth > progressFifth && fifth < 5) {
            progressFifth = fifth;
            Log.debug("OTA: %s — %u %%\n", progressWhat, (unsigned)(fifth * 20));
        }
    });

    size_t written = Update.writeStream(*http.getStreamPtr());
    // Update.end() при заданной setMD5 сам сверяет сумму, поэтому успех здесь
    // означает и «дописано до конца», и «сумма сошлась».
    bool ok = written == (size_t)len && Update.end();
    uint32_t ms = millis() - startMs;
    if (ok) {
        logDone(what, (unsigned)written, ms);
    } else {
        Log.error("OTA: %s — ошибка на %u из %d байт за %u,%u с: %s\n", what, (unsigned)written, len,
                  (unsigned)(ms / 1000), (unsigned)(ms % 1000 / 100), Update.errorString());
        Update.abort();
    }
    http.end();
    return ok;
}

}  // namespace

uint8_t run(const core::OtaRequest& req) {
    Log.printf("OTA: обновление из облака — образов %u\n",
               (unsigned)(req.filesystem.present + req.firmware.present));
    if (req.filesystem.present && !flash(req.filesystem, U_SPIFFS)) {
        Log.error("OTA: прервано на ФС, прошивку не трогаем — работаем на старой\n");
        return core::OTA_ERR_FS;
    }
    if (req.firmware.present && !flash(req.firmware, U_FLASH)) {
        Log.error("OTA: прервано на прошивке — перезагрузки не будет\n");
        return core::OTA_ERR_FIRMWARE;
    }
    Log.println("OTA: готово, перезагрузка");
    storage::saveRestartReason(core::RestartReason::OtaCloud);
    delay(300);
    ESP.restart();
    return core::OTA_OK;  // недостижимо: ESP.restart() не возвращается, но не объявлен noreturn
}

}  // namespace ota_cloud
