// Порт: NVS. Настройки, последнее успешное чтение счётчика, код ошибки OTA.
// Не LittleFS: обновление образа ФС стирает раздел целиком.
#pragma once
#include <stdint.h>

#include "../core/meter.h"
#include "../core/settings.h"

namespace storage {

// Нет записи, другой размер или версия — умолчания.
void loadSettings(core::Settings& s);
void saveSettings(const core::Settings& s);

// Канал и BSSID лежат отдельным ключом: они меняются при каждом роуминге, а
// переписывать вместе с ними весь блоб настроек (пароль счётчика, ключ облака)
// ради этого незачем. loadSettings() накладывает эту пару поверх блоба.
void saveFastConnect(const core::Settings& s);

// false — успешных чтений ещё не было. readAt — UTC epoch, 0 если время было неизвестно.
bool loadLastReading(core::MeterData& m, uint32_t& readAt);
void saveLastReading(const core::MeterData& m, uint32_t readAt);

uint8_t loadOtaError();
void saveOtaError(uint8_t code);

// Сброс к заводским: стирает всё пространство имён.
void resetAll();

}  // namespace storage
