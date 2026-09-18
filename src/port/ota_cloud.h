// Порт: OTA через сервер Waterius — как ESP8266/src/ota_update.cpp в waterius,
// но свой загрузчик: HTTPUpdate в arduino-esp32 2.0.17 не умеет setMD5sum.
#pragma once
#include "../core/cloud.h"

namespace ota_cloud {

// Сначала образ ФС, потом прошивка, затем перезагрузка.
// Возвращается только при ошибке — с кодом core::OtaError для поля ota_error.
uint8_t run(const core::OtaRequest& req);

}  // namespace ota_cloud
