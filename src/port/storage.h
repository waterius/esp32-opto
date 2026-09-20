// Порт: NVS. Настройки, последнее успешное чтение счётчика, код ошибки OTA.
// Не LittleFS: обновление образа ФС стирает раздел целиком.
#pragma once
#include <stdint.h>

#include "../core/meter.h"
#include "../core/restart_reason.h"
#include "../core/settings.h"

namespace storage {

// Настройки лежат ключом `cfg` как JSON (core/settings_io.*): запись
// самоописательная, поэтому новое поле берёт умолчание само и обновление
// прошивки не стоит ни одной настройки. Блоб `settings` прежних прошивок
// переносится один раз при первой загрузке. Записи нет или она не разбирается —
// умолчания.
void loadSettings(core::Settings& s);

// false — настройки не сохранились (не поместились в буфер или NVS отказала);
// прежняя запись при этом цела. Вызывающий обязан сказать об этом в лог.
bool saveSettings(const core::Settings& s);

// Канал и BSSID лежат отдельным ключом: они меняются при каждом роуминге, а
// переписывать вместе с ними всю запись настроек (пароль счётчика, ключ облака)
// ради этого незачем. loadSettings() накладывает эту пару поверх записи.
void saveFastConnect(const core::Settings& s);

// Счётчик загрузок подряд без признака «загрузка удалась»: защита от кирпича
// после неудачной прошивки (safe mode).
uint8_t loadBootCount();
void saveBootCount(uint8_t count);

// Кто перезагрузил плату. Пишется из loop() перед самой перезагрузкой,
// читается и сбрасывается на следующем старте. Сторож цикла сюда писать не
// может — он работает, когда loop() уже не жив, и оставляет метку в RTC.
core::RestartReason loadRestartReason();
void saveRestartReason(core::RestartReason reason);

// false — успешных чтений ещё не было. readAt — UTC epoch, 0 если время было неизвестно.
bool loadLastReading(core::MeterData& m, uint32_t& readAt);
void saveLastReading(const core::MeterData& m, uint32_t readAt);

uint8_t loadOtaError();
void saveOtaError(uint8_t code);

// Сброс к заводским: стирает всё пространство имён.
void resetAll();

}  // namespace storage
