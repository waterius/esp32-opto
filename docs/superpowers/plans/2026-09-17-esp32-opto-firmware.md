# Прошивка esp32-opto — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Прошивка для ESP32-S3 и ESP32-C3: чтение счётчика НАРТИС-100 через оптопорт, отправка показаний в облако Waterius, веб-морда из четырёх страниц (статус, настройки, Wi-Fi, лог), прозрачный serial по RFC 2217 в локальной сети и OTA.

**Architecture:** Ядро `src/core/` без Arduino: адаптер счётчика на GuruxDLMS.c, модель настроек, обмен с облаком. Порт `src/port/`: UART с арбитром `OptoBus`, Wi-Fi-супервизор, веб на ESPAsyncWebServer, RFC 2217, NVS, OTA. Весь наш код работает в `loop()`; веб-хендлеры и колбэки сервера RFC 2217 выполняются в чужих задачах и только ставят флаги. Вывод идёт через `Log` — в USB и в кольцевой буфер, который показывает страница лога.

**Tech Stack:** PlatformIO, Arduino (arduino-esp32 2.0.17), ESP32Async/ESPAsyncWebServer + AsyncTCP, GuruxDLMS.c (форк latonita), ElegantOTA 4, igrr/rfc2217-server (копия с патчем), ArduinoJson 7.

**Spec:** `docs/superpowers/specs/2026-09-17-esp32-opto-firmware-design.md`

## Global Constraints

- `platform = espressif32@6.12.0`, `framework = arduino`, `board_build.filesystem = littlefs`.
- env `esp32-s3`: плата `esp32-s3-devkitc-1`, разделы `default_8MB.csv` (по умолчанию), оптопорт RX GPIO17 / TX GPIO18, BOOT GPIO0.
- env `esp32-c3`: плата `esp32-c3-devkitm-1`, `board_build.partitions = min_spiffs.csv`, `-DARDUINO_USB_MODE=1`, оптопорт RX GPIO4 / TX GPIO5, BOOT GPIO9.
- OTA обязателен на обеих платах: в таблице разделов два слота приложения.
- `src/core/` не включает заголовки Arduino и ESP-IDF; C-библиотека Gurux и ArduinoJson допустимы.
- Веб-хендлеры и колбэки RFC 2217 только читают `app` и ставят атомарные флаги. UART, NVS, TLS и переключения Wi-Fi — только из `loop()`.
- Пароль счётчика никогда не перебирается: `ReadResult::AuthRejected` выключает опрос (`meterEnabled = false`). После 5 неверных паролей счётчик блокирует интерфейсы на сутки.
- Юнит-тесты — только у протокола обмена со счётчиком (спека, раздел 12): `~/.platformio/penv/bin/pio test -e native`, папка `test/test_nartis`. В задаче 1 они не собираются (старый адаптер удалён), с задачи 2 обязательны. Каждая задача проверяется сборкой обеих плат командой `~/.platformio/penv/bin/pio run -e esp32-s3 -e esp32-c3`: код возврата 0, оба env `SUCCESS`. **После `pio` в конвейере не ставить `grep`/`tail`** — код возврата будет от них. Шаги «на железе» выполняет владелец устройства.
- Комментарии, логи, интерфейс и сообщения коммитов — на русском; коммиты — conventional commits.
- Весь вывод — через `Log` из `src/port/log.h` (`Log.printf`, `Log.println`), не через `Serial`: иначе сообщение не попадёт на страницу лога. `Log` можно звать из любой задачи. Каждая строка лога начинается с `[секунды.мс] `; в ожидаемом выводе задач 2–7 эта метка опущена.
- Код во всех задачах уже собран по стадиям при подготовке плана (обе платы, код возврата 0): копировать как есть.

## Отступления от спеки

- Автомат опроса вынесен из `main.cpp` в `src/poller.*`.
- Добавлен `tools/fake_cloud.py` — заглушка облака; спека (раздел 12) требует локальный HTTP-сервер для проверки OTA.
- Флаги `DLMS_IGNORE_*` в `platformio.ini` не нужны: на ESP32 `gxignore.h` сам подключает `ArduinoIgnore.h`, где они заданы.
- Сервер igrr вызывает `on_client_connected` только после согласования RFC 2217. Патч переносит вызов на `accept()`: по спеке опрос прерывает любое подключение к TCP-порту.
- Первый цикл опроса — через 30 секунд после старта, дальше по периоду: иначе после перепрошивки пришлось бы ждать целый период.

## Карта файлов

| Файл | Отвечает за | Задача |
|---|---|---|
| `platformio.ini` | envs, библиотеки, пины, версия прошивки | 1 |
| `src/core/opto_port.h` | `IOptoPort`, `SerialCfg`, время | 1 |
| `src/core/meter.h` | `MeterData`, `ReadResult`, `IMeter` | 1 |
| `src/core/settings.h` | `Settings`, `SETTINGS_VERSION` | 1 |
| `src/core/cloud.*` | тело запроса в облако, разбор блока `ota` | 1 |
| `src/port/opto_esp32.*` | UART1 | 1 |
| `src/port/opto_bus.*` | владелец UART1, арбитр опроса и сессии | 1 |
| `src/port/storage.*` | NVS: настройки, последнее чтение, `ota_error` | 1 |
| `src/port/log.*` | лог: USB и кольцевой буфер 16 КБ | 1 |
| `src/app.h` | общее состояние и флаги запросов | 1 |
| `src/port/hal_esp32.cpp` | `nowMs` / `sleepMs` — не меняется | — |
| `src/core/nartis.*` | адаптер НАРТИС на Gurux | 2 |
| `src/port/net.*` | Wi-Fi-супервизор, HTTPS-клиент | 3 |
| `src/port/wifi_portal.*` | скан сетей, подключение, captive portal | 3 |
| `src/port/web.*` | веб-сервер и JSON API, `/api/log` | 3, 4, 5, 6 |
| `data/*` | страницы, скрипт, стили | 3, 4 |
| `src/poller.*` | автомат опроса и отправки | 4, 6 |
| `tools/fake_cloud.py` | заглушка облака | 4 |
| `lib/rfc2217-server/` | сервер RFC 2217 с патчем | 5 |
| `src/port/rfc2217.*` | прозрачный serial | 5 |
| `src/port/ota_cloud.*` | OTA через сервер Waterius | 6 |
| `test/test_nartis/` | юнит-тесты протокола: эмулятор счётчика и реальный дамп | 2 |
| `src/main.cpp` | сборка модулей в `setup` / `loop` | все |

---

## Задача 1: Фундамент — библиотеки, модель настроек, NVS, арбитр оптопорта, лог

**Files:**
- Delete: `src/settings.h`, `src/core/dlms.h`, `src/core/dlms.cpp`, `src/core/nartis.h`, `src/core/nartis.cpp`, `src/port/settings_nvs.cpp`, `src/port/net.h`, `src/port/net.cpp`, `src/port/web.h`, `src/port/web.cpp`, `src/port/rfc2217.h`, `src/port/rfc2217.cpp`
- Modify (заменить целиком): `platformio.ini`, `src/core/opto_port.h`, `src/core/meter.h`, `src/core/cloud.h`, `src/core/cloud.cpp`, `src/port/opto_esp32.h`, `src/port/opto_esp32.cpp`, `src/app.h`, `src/main.cpp`
- Create: `src/core/settings.h`, `src/port/opto_bus.h`, `src/port/opto_bus.cpp`, `src/port/storage.h`, `src/port/storage.cpp`, `src/port/log.h`, `src/port/log.cpp`

**Interfaces:**
- Consumes: —
- Produces:
  - `core::SerialCfg {baud, bits, parity, stop}`, `operator==`; `core::IOptoPort`: `configure(const SerialCfg&)`, `write(const uint8_t*, size_t) -> size_t`, `read() -> int`, `available() -> int`, `flushInput()`, `abortRequested() -> bool`; `core::nowMs()`, `core::sleepMs(uint32_t)`.
  - `core::MeterData {total, tariff[4], tariffCount, serial[20], model[40], fwVersion[20], time[24]}`; `enum class core::ReadResult {Ok, Failed, AuthRejected, Aborted}`; `core::IMeter::read(MeterData&, char* error, size_t errorCap) -> ReadResult`.
  - `core::Settings` (поля — в `src/core/settings.h`), `core::SETTINGS_VERSION = 2`.
  - `core::buildCloudPayload(const MeterData&, uint32_t readAt, const Settings&, const DeviceInfo&, char* out, size_t cap) -> size_t`; `core::DeviceInfo {fw, ip, rssi, chipId, otaError}`; `core::parseOta(const char* body, OtaRequest&) -> OtaParse {None, Ok, Error}`; `core::OtaRequest {OtaImage firmware, filesystem}`, `core::OtaImage {present, url[256], md5[33]}`; `enum core::OtaError {OTA_OK, OTA_ERR_PARSE, OTA_ERR_FS, OTA_ERR_FIRMWARE}`.
  - `extern OptoBus bus` — `core::IOptoPort` плюс `begin(const SerialCfg&)`, `current() -> const SerialCfg&`, `owner() -> BusOwner {Free, Meter, Transparent}`, `acquireForMeter() -> bool`, `releaseMeter()`, `beginTransparent()`, `endTransparent()`, `requestPreempt()`.
  - `storage::loadSettings(Settings&)`, `saveSettings(const Settings&)`, `loadLastReading(MeterData&, uint32_t& readAt) -> bool`, `saveLastReading(const MeterData&, uint32_t)`, `loadOtaError() -> uint8_t`, `saveOtaError(uint8_t)`, `resetAll()`.
  - `extern LogSink Log` (наследник `Print`): `begin(unsigned long baud)` вместо `Serial.begin`, `print/printf/println`, `read(uint32_t from, char* out, size_t cap, size_t& len, bool& skipped) -> uint32_t next`, `bootId() -> uint32_t`, `LogSink::SIZE = 16384`.
  - `extern AppState app` — поля в `src/app.h`.

- [ ] **Шаг 1: Удалить старые файлы**

Старые `dlms`, `nartis`, `web` и `rfc2217` — рукописные реализации, которые по спеке заменяются библиотеками; `net` переписывается в задаче 3. Тесты `test/test_nartis` не удаляются: в задаче 2 они проверяют новый адаптер. До задачи 2 `pio test -e native` не собирается — адаптера нет.

```bash
git rm src/settings.h src/core/dlms.h src/core/dlms.cpp src/core/nartis.h src/core/nartis.cpp \
       src/port/settings_nvs.cpp src/port/net.h src/port/net.cpp src/port/web.h src/port/web.cpp \
       src/port/rfc2217.h src/port/rfc2217.cpp
```

- [ ] **Шаг 2: Заменить `platformio.ini` целиком**

Все библиотеки подключаются сразу: неиспользуемые не попадают в бинарник. Коммит GuruxDLMS_c зафиксирован — это форк с `library.json`, которым пользуется latonita.

```ini
; esp32-opto — счётчик НАРТИС через оптопорт + облако Waterius
;
; Сборка:   ~/.platformio/penv/bin/pio run -e esp32-s3 -e esp32-c3
; Прошивка: ~/.platformio/penv/bin/pio run -e esp32-s3 -t upload
; Образ ФС: ~/.platformio/penv/bin/pio run -e esp32-s3 -t uploadfs
; Монитор:  ~/.platformio/penv/bin/pio device monitor
; Тесты:    ~/.platformio/penv/bin/pio test -e native   (с задачи 2)
;
; Результат сборки проверять по коду возврата pio, без grep/tail после него.

[platformio]
; native — только для тестов, `pio run` без -e его не собирает
default_envs = esp32-s3, esp32-c3

[firmware]
; уходит в облако полем fw; сервер Waterius сравнивает его с целевой версией OTA
version = "\"0.2.0\""

[env]
platform = espressif32@6.12.0
framework = arduino
board_build.filesystem = littlefs
monitor_speed = 115200
lib_deps =
    ; та же версия, что в прошивке Waterius
    ArduinoJson@7.3.1
    esp32async/ESPAsyncWebServer@^3.12.1
    esp32async/AsyncTCP@^3.5.0
    ayushsharma82/ElegantOTA@^4.0.0
    https://github.com/latonita/GuruxDLMS_c.git#a8c1b92564733d1921d77f17e5e1d8d4bf73f2a7
build_flags =
    -DCORE_DEBUG_LEVEL=3
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DELEGANTOTA_USE_ASYNC_WEBSERVER=1
    -DFIRMWARE_VERSION=${firmware.version}

[env:esp32-s3]
board = esp32-s3-devkitc-1
build_flags =
    ${env.build_flags}
    -DOPTO_RX_PIN=17
    -DOPTO_TX_PIN=18
    -DBOOT_PIN=0

[env:esp32-c3]
board = esp32-c3-devkitm-1
; default.csv даёт всего 1.25 МБ на приложение — мало. min_spiffs:
; app0/app1 по 1.875 МБ (OTA сохраняется) + 128 КБ под LittleFS.
board_build.partitions = min_spiffs.csv
build_flags =
    ${env.build_flags}
    ; У C3 USB — это USB-Serial/JTAG (HWCDC). Без ARDUINO_USB_MODE=1
    ; Serial не объявляется ни в HWCDC.h, ни в USBCDC.h.
    ; У платы esp32-s3-devkitc-1 этот флаг уже есть в описании.
    -DARDUINO_USB_MODE=1
    -DOPTO_RX_PIN=4
    -DOPTO_TX_PIN=5
    -DBOOT_PIN=9

; Юнит-тесты протокола обмена со счётчиком на компьютере, без платы.
; Из src собирается только адаптер: он не зависит от Arduino.
[env:native]
platform = native
framework =
lib_deps =
    https://github.com/latonita/GuruxDLMS_c.git#a8c1b92564733d1921d77f17e5e1d8d4bf73f2a7
build_flags = -std=gnu++17 -Wall -DUNITY_INCLUDE_DOUBLE
test_framework = unity
test_build_src = yes
build_src_filter = -<*> +<core/nartis.cpp>
```

- [ ] **Шаг 3: Ядро — `src/core/opto_port.h` (заменить целиком)**

`abortRequested()` — сигнал адаптеру бросить обмен, когда порт забрала прозрачная сессия.

```cpp
// Ядро: интерфейс оптопорта. Реализация — port/opto_bus.*.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace core {

// Параметры COM-порта. parity: 'N', 'E', 'O'.
struct SerialCfg {
    uint32_t baud = 9600;
    uint8_t bits = 8;
    char parity = 'N';
    uint8_t stop = 1;
};

inline bool operator==(const SerialCfg& a, const SerialCfg& b) {
    return a.baud == b.baud && a.bits == b.bits && a.parity == b.parity && a.stop == b.stop;
}

class IOptoPort {
   public:
    virtual ~IOptoPort() {}
    virtual void configure(const SerialCfg& cfg) = 0;
    virtual size_t write(const uint8_t* data, size_t len) = 0;
    virtual int read() = 0;  // -1, если пусто
    virtual int available() = 0;
    virtual void flushInput() = 0;
    // Порт забрала прозрачная сессия: текущий обмен надо бросить сразу.
    virtual bool abortRequested() = 0;
};

// Время — тоже из порта, чтобы ядро не зависело от Arduino.
uint32_t nowMs();
void sleepMs(uint32_t ms);

}  // namespace core
```

- [ ] **Шаг 4: Ядро — `src/core/meter.h` (заменить целиком)**

```cpp
// Ядро: данные счётчика и интерфейс адаптера.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "opto_port.h"

namespace core {

const uint8_t MAX_TARIFFS = 4;

struct MeterData {
    double total = 0;                  // суммарный расход, кВт·ч
    double tariff[MAX_TARIFFS] = {0};  // T1..T4, кВт·ч
    uint8_t tariffCount = 0;           // сколько тарифов прочитано
    char serial[20] = {0};             // серийный номер счётчика
    char model[40] = {0};              // тип счётчика, UTF-8
    char fwVersion[20] = {0};          // версия ПО счётчика
    char time[24] = {0};               // время счётчика, "ГГГГ-ММ-ДД ЧЧ:ММ:СС"
};

enum class ReadResult {
    Ok,
    Failed,        // нет ответа или ошибка протокола — повтор через 5 минут
    AuthRejected,  // счётчик отверг пароль — опрос выключается
    Aborted,       // порт забрала прозрачная сессия
};

class IMeter {
   public:
    virtual ~IMeter() {}
    // Полный цикл: связь, чтение, разрыв. error — текст для страницы статуса.
    virtual ReadResult read(MeterData& out, char* error, size_t errorCap) = 0;
};

}  // namespace core
```

- [ ] **Шаг 5: Ядро — создать `src/core/settings.h`**

Структура пишется в NVS целиком; `version` и размер сверяются при чтении.

```cpp
// Ядро: модель настроек устройства. Хранение — port/storage.*.
#pragma once
#include <stdint.h>

#include "opto_port.h"

namespace core {

// Менять при любом изменении структуры: старые настройки из NVS тогда
// заменятся умолчаниями, без миграции.
const uint16_t SETTINGS_VERSION = 2;

struct Settings {
    uint16_t version = SETTINGS_VERSION;

    // Wi-Fi
    char ssid[33] = "";
    char pass[65] = "";
    uint8_t bssid[6] = {0};  // быстрый коннект, как в waterius
    uint8_t channel = 0;     // 0 — канал неизвестен, полный скан

    // Оптопорт
    SerialCfg serial;  // умолчания 9600 8N1 — параметры НАРТИС

    // Счётчик
    bool meterEnabled = true;   // тумблер «в работе»
    uint8_t meterAddr = 0;      // 0 = перебор 16 → 17
    char meterPwd[17] = "111";  // пароль LLS чтения, клиент 32 (заводской у серии 100)

    // Облако Waterius
    uint16_t periodMin = 60;
    char host[64] = "https://cloud.waterius.ru";
    char key[41] = "";
    char email[64] = "";

    // Прозрачный serial
    bool rfcEnabled = true;
    uint16_t rfcPort = 2217;
};

}  // namespace core
```

- [ ] **Шаг 6: Ядро — `src/core/cloud.h` и `src/core/cloud.cpp` (заменить целиком)**

Поля запроса — по `IZSerializer` бэкенда; `meter_fw`, `meter_time`, `meter_read_at`, `ota_error` бэкенд пока игнорирует. Формат блока `ota` — как в waterius `ESP8266/src/ota_parse.h`.

```cpp
// Ядро: обмен с облаком Waterius — тело запроса POST /api/source/iz/ и разбор
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
```

```cpp
#include "cloud.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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
    doc["total"] = m.total;
    doc["data_type"] = DT_ELECTRICITY;

    for (uint8_t i = 0; i < m.tariffCount && i < MAX_TARIFFS; i++) {
        char name[12];
        snprintf(name, sizeof(name), "total%u", i + 1);
        doc[name] = m.tariff[i];
        snprintf(name, sizeof(name), "data_type%u", i + 1);
        doc[name] = tariffDataType(i);
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
```

- [ ] **Шаг 7: Порт — `src/port/opto_esp32.h` и `.cpp` (заменить целиком)**

`OptoEsp32` больше не реализует `IOptoPort` и не имеет глобального экземпляра: им владеет `OptoBus`.

```cpp
// Порт: UART1 оптопорта. Пины — флаги OPTO_RX_PIN / OPTO_TX_PIN в platformio.ini.
// Напрямую им пользуется только OptoBus (opto_bus.h).
#pragma once
#include <stddef.h>

#include "../core/opto_port.h"

class OptoEsp32 {
   public:
    void begin(const core::SerialCfg& cfg);
    void configure(const core::SerialCfg& cfg);
    size_t write(const uint8_t* data, size_t len);
    int read();
    int available();
    void flushInput();

    const core::SerialCfg& current() const { return cfg_; }

   private:
    core::SerialCfg cfg_;
    bool started_ = false;
};
```

```cpp
#include "opto_esp32.h"

#include <Arduino.h>
#include <HardwareSerial.h>

#ifndef OPTO_RX_PIN
#define OPTO_RX_PIN 17
#endif
#ifndef OPTO_TX_PIN
#define OPTO_TX_PIN 18
#endif

namespace {

HardwareSerial uart(1);

// Слово конфигурации UART из «человеческих» параметров.
uint32_t configWord(const core::SerialCfg& c) {
    const bool two = c.stop == 2;
    switch (c.bits) {
        case 7:
            if (c.parity == 'E') return two ? SERIAL_7E2 : SERIAL_7E1;
            if (c.parity == 'O') return two ? SERIAL_7O2 : SERIAL_7O1;
            return two ? SERIAL_7N2 : SERIAL_7N1;
        case 6:
            if (c.parity == 'E') return two ? SERIAL_6E2 : SERIAL_6E1;
            if (c.parity == 'O') return two ? SERIAL_6O2 : SERIAL_6O1;
            return two ? SERIAL_6N2 : SERIAL_6N1;
        case 5:
            if (c.parity == 'E') return two ? SERIAL_5E2 : SERIAL_5E1;
            if (c.parity == 'O') return two ? SERIAL_5O2 : SERIAL_5O1;
            return two ? SERIAL_5N2 : SERIAL_5N1;
        default:
            if (c.parity == 'E') return two ? SERIAL_8E2 : SERIAL_8E1;
            if (c.parity == 'O') return two ? SERIAL_8O2 : SERIAL_8O1;
            return two ? SERIAL_8N2 : SERIAL_8N1;
    }
}

}  // namespace

void OptoEsp32::begin(const core::SerialCfg& cfg) {
    cfg_ = cfg;
    uart.begin(cfg.baud, configWord(cfg), OPTO_RX_PIN, OPTO_TX_PIN);
    started_ = true;
}

void OptoEsp32::configure(const core::SerialCfg& cfg) {
    if (started_ && cfg_ == cfg) return;
    if (started_) uart.end();
    begin(cfg);
}

size_t OptoEsp32::write(const uint8_t* data, size_t len) { return uart.write(data, len); }
int OptoEsp32::read() { return uart.read(); }
int OptoEsp32::available() { return uart.available(); }

void OptoEsp32::flushInput() {
    while (uart.available()) uart.read();
}
```

- [ ] **Шаг 8: Порт — создать `src/port/opto_bus.h` и `.cpp`**

Шина пишет в лог каждый байт оптопорта — и опроса, и прозрачной сессии: `TX` при записи (по 20 байт в строке), `RX` — когда во входном буфере UART не осталось данных, набралось 20 байт или началась запись. UART ядра отдаёт принятое порциями после паузы в 2 символа, поэтому строка `RX` обычно совпадает с кадром. Смена параметров порта тоже попадает в лог.

```cpp
// Порт: единственный владелец оптопорта (UART1) — арбитр опроса и прозрачной сессии.
// Все байты, прошедшие через шину, пишутся в лог строками TX/RX в hex.
// Все методы, кроме requestPreempt(), вызываются только из loop().
#pragma once
#include <atomic>

#include "../core/opto_port.h"

enum class BusOwner : uint8_t { Free, Meter, Transparent };

class OptoBus : public core::IOptoPort {
   public:
    void begin(const core::SerialCfg& cfg);

    // IOptoPort — для адаптера счётчика
    void configure(const core::SerialCfg& cfg) override;
    size_t write(const uint8_t* data, size_t len) override;
    int read() override;
    int available() override;
    void flushInput() override;
    bool abortRequested() override { return preempt_.load(); }

    const core::SerialCfg& current() const;
    BusOwner owner() const { return owner_; }

    // Опрос счётчика: взять шину, только если она свободна и никто не ждёт.
    bool acquireForMeter();
    void releaseMeter();

    // Прозрачная сессия.
    void beginTransparent();
    void endTransparent();  // заодно снимает запрос на прерывание

    // Из колбэка подключения клиента (поток сервера RFC 2217):
    // идущий обмен со счётчиком должен прерваться.
    void requestPreempt() { preempt_.store(true); }

   private:
    BusOwner owner_ = BusOwner::Free;
    std::atomic<bool> preempt_{false};
};

extern OptoBus bus;
```

```cpp
#include "opto_bus.h"

#include "log.h"
#include "opto_esp32.h"

OptoBus bus;

namespace {

OptoEsp32 uart;

// Байты оптопорта в лог: строка на направление, не длиннее LINE байт.
// Принятые копятся, пока во входном буфере UART есть данные, — обычно строка = кадр.
const size_t LINE = 20;
uint8_t rxPending[LINE];
size_t rxCount = 0;

void logBytes(const char* dir, const uint8_t* data, size_t len) {
    char line[3 + LINE * 3 + 1];
    for (size_t off = 0; off < len; off += LINE) {
        size_t n = len - off < LINE ? len - off : LINE;
        int k = snprintf(line, sizeof(line), "%s", dir);
        for (size_t i = 0; i < n; ++i) k += snprintf(line + k, sizeof(line) - k, " %02x", data[off + i]);
        Log.println(line);
    }
}

void flushRx() {
    if (!rxCount) return;
    logBytes("RX", rxPending, rxCount);
    rxCount = 0;
}

}  // namespace

void OptoBus::begin(const core::SerialCfg& cfg) { uart.begin(cfg); }

void OptoBus::configure(const core::SerialCfg& cfg) {
    flushRx();
    if (!(cfg == uart.current()))
        Log.printf("Оптопорт: %lu %u%c%u\n", (unsigned long)cfg.baud, cfg.bits, cfg.parity, cfg.stop);
    uart.configure(cfg);
}

size_t OptoBus::write(const uint8_t* data, size_t len) {
    flushRx();
    logBytes("TX", data, len);
    return uart.write(data, len);
}

int OptoBus::read() {
    int c = uart.read();
    if (c >= 0) {
        rxPending[rxCount++] = (uint8_t)c;
        if (rxCount == LINE) flushRx();
    }
    return c;
}

int OptoBus::available() {
    int n = uart.available();
    if (n <= 0) flushRx();  // входной буфер пуст — принятая порция закончилась
    return n;
}

void OptoBus::flushInput() {
    flushRx();
    uart.flushInput();
}

const core::SerialCfg& OptoBus::current() const { return uart.current(); }

bool OptoBus::acquireForMeter() {
    if (owner_ != BusOwner::Free || preempt_.load()) return false;
    owner_ = BusOwner::Meter;
    return true;
}

void OptoBus::releaseMeter() {
    flushRx();
    if (owner_ == BusOwner::Meter) owner_ = BusOwner::Free;
}

void OptoBus::beginTransparent() {
    flushRx();
    owner_ = BusOwner::Transparent;
    uart.flushInput();
}

void OptoBus::endTransparent() {
    flushRx();
    owner_ = BusOwner::Free;
    preempt_.store(false);
}
```

- [ ] **Шаг 9: Порт — создать `src/port/storage.h` и `.cpp`**

```cpp
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

// false — успешных чтений ещё не было. readAt — UTC epoch, 0 если время было неизвестно.
bool loadLastReading(core::MeterData& m, uint32_t& readAt);
void saveLastReading(const core::MeterData& m, uint32_t readAt);

uint8_t loadOtaError();
void saveOtaError(uint8_t code);

// Сброс к заводским: стирает всё пространство имён.
void resetAll();

}  // namespace storage
```

```cpp
#include "storage.h"

#include <Preferences.h>

namespace storage {
namespace {

const char* NS = "opto";
const char* KEY_SETTINGS = "settings";
const char* KEY_READING = "reading";
const char* KEY_OTA_ERROR = "ota_error";

struct StoredReading {
    core::MeterData data;
    uint32_t readAt;
};

// Размер сверяется: запись от другой версии структуры не читается.
bool loadBlob(const char* key, void* dst, size_t size) {
    Preferences p;
    if (!p.begin(NS, true)) return false;
    bool ok = p.getBytesLength(key) == size && p.getBytes(key, dst, size) == size;
    p.end();
    return ok;
}

void saveBlob(const char* key, const void* src, size_t size) {
    Preferences p;
    if (!p.begin(NS, false)) return;
    p.putBytes(key, src, size);
    p.end();
}

}  // namespace

void loadSettings(core::Settings& s) {
    core::Settings stored;
    if (loadBlob(KEY_SETTINGS, &stored, sizeof(stored)) && stored.version == core::SETTINGS_VERSION) {
        s = stored;
    } else {
        s = core::Settings();
    }
}

void saveSettings(const core::Settings& s) { saveBlob(KEY_SETTINGS, &s, sizeof(s)); }

bool loadLastReading(core::MeterData& m, uint32_t& readAt) {
    StoredReading r;
    if (!loadBlob(KEY_READING, &r, sizeof(r))) return false;
    m = r.data;
    readAt = r.readAt;
    return true;
}

void saveLastReading(const core::MeterData& m, uint32_t readAt) {
    StoredReading r;
    r.data = m;
    r.readAt = readAt;
    saveBlob(KEY_READING, &r, sizeof(r));
}

uint8_t loadOtaError() {
    Preferences p;
    if (!p.begin(NS, true)) return 0;
    uint8_t v = p.getUChar(KEY_OTA_ERROR, 0);
    p.end();
    return v;
}

void saveOtaError(uint8_t code) {
    Preferences p;
    if (!p.begin(NS, false)) return;
    p.putUChar(KEY_OTA_ERROR, code);
    p.end();
}

void resetAll() {
    Preferences p;
    if (!p.begin(NS, false)) return;
    p.clear();
    p.end();
}

}  // namespace storage
```

- [ ] **Шаг 10: Порт — создать `src/port/log.h` и `.cpp`**

Кольцевой буфер хранит последние 16 КБ вывода (один опрос счётчика в hex — около 4 КБ); страница лога (задача 3) забирает из него текст по сквозной позиции. Мьютекс нужен, потому что в лог пишут и `loop()`, и колбэки RFC 2217, а читает веб-хендлер. Буфер опустошается только перезагрузкой.

```cpp
// Порт: лог прошивки. Пишет в USB (Serial) и в кольцевой буфер, который
// показывает страница log.html. Вызывается из любой задачи: запись под мьютексом.
#pragma once
#include <Arduino.h>

class LogSink : public Print {
   public:
    static constexpr size_t SIZE = 16384;

    // Вместо Serial.begin(): до вызова лог не защищён мьютексом.
    void begin(unsigned long baud);

    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t* data, size_t len) override;

    // Копирует в out текст, записанный после позиции from (сквозной счёт байт с
    // момента старта), и возвращает позицию для следующего запроса. Если часть
    // уже затёрта, skipped = true и копирование начинается с первой целой строки.
    uint32_t read(uint32_t from, char* out, size_t cap, size_t& len, bool& skipped);

    // Меняется при каждой загрузке: так страница узнаёт о перезагрузке.
    uint32_t bootId() const { return bootId_; }

   private:
    void put(const uint8_t* data, size_t len);

    char buf_[SIZE];
    uint32_t total_ = 0;  // сколько байт записано с момента старта
    bool lineStart_ = true;
    uint32_t bootId_ = 0;
    SemaphoreHandle_t mutex_ = nullptr;
};

extern LogSink Log;
```

```cpp
#include "log.h"

LogSink Log;

void LogSink::begin(unsigned long baud) {
    Serial.begin(baud);
    bootId_ = esp_random();
    mutex_ = xSemaphoreCreateMutex();
}

size_t LogSink::write(const uint8_t* data, size_t len) {
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    size_t i = 0;
    while (i < len) {
        if (lineStart_) {
            // Каждая строка начинается со времени от старта: [секунды.миллисекунды]
            char stamp[20];
            unsigned long ms = millis();
            int n = snprintf(stamp, sizeof(stamp), "[%lu.%03lu] ", ms / 1000, ms % 1000);
            put((const uint8_t*)stamp, n);
            lineStart_ = false;
        }
        const uint8_t* nl = (const uint8_t*)memchr(data + i, '\n', len - i);
        size_t n = nl ? (size_t)(nl - data) + 1 - i : len - i;
        put(data + i, n);
        lineStart_ = nl != nullptr;
        i += n;
    }
    if (mutex_) xSemaphoreGive(mutex_);
    return len;
}

void LogSink::put(const uint8_t* data, size_t len) {
    Serial.write(data, len);
    while (len) {
        size_t pos = total_ % SIZE;
        size_t chunk = min(len, SIZE - pos);
        memcpy(buf_ + pos, data, chunk);
        total_ += chunk;
        data += chunk;
        len -= chunk;
    }
}

uint32_t LogSink::read(uint32_t from, char* out, size_t cap, size_t& len, bool& skipped) {
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    uint32_t oldest = total_ > SIZE ? total_ - SIZE : 0;
    if (from > total_) from = 0;  // позиция из прошлой загрузки
    skipped = false;
    uint32_t pos = from;
    if (pos < oldest) {
        skipped = from > 0;
        // Старейшая строка обрезана буфером (возможно, посреди буквы UTF-8)
        for (pos = oldest; pos < total_ && buf_[pos % SIZE] != '\n';) ++pos;
        if (pos < total_) ++pos;
    }
    len = 0;
    while (pos < total_ && len < cap) out[len++] = buf_[pos++ % SIZE];
    if (mutex_) xSemaphoreGive(mutex_);
    return pos;
}
```

- [ ] **Шаг 11: `src/app.h` и `src/main.cpp` (заменить целиком)**

`main.cpp` на этом шаге только загружает настройки и открывает UART.

```cpp
// Общее состояние прошивки. Пишет loop(); веб-хендлеры читают его без блокировок
// (это только отображение) и выставляют атомарные флаги запросов.
#pragma once
#include <atomic>

#include "core/meter.h"
#include "core/settings.h"

struct AppState {
    core::Settings sett;

    // Последнее успешное чтение счётчика (хранится в NVS)
    core::MeterData last;
    uint32_t lastReadAt = 0;  // UTC epoch; 0 — время было неизвестно
    bool hasReading = false;

    std::atomic<bool> meterReading{false};
    char meterError[64] = "";  // пусто — последнее чтение без ошибок

    uint32_t cloudAt = 0;       // UTC epoch последней успешной отправки
    int cloudCode = 0;          // HTTP-код последней попытки; <0 — нет соединения; 0 — не отправляли
    char cloudError[48] = "";   // пусто — последняя отправка успешна
    uint8_t otaError = 0;       // код ошибки OTA через сервер, уходит полем ota_error

    // Запросы с веб-страниц: поток async_tcp → loop()
    std::atomic<bool> readNow{false};
    std::atomic<bool> sendNow{false};
    std::atomic<bool> rebootNow{false};
    std::atomic<bool> settingsPending{false};
    core::Settings pendingSettings;
};

extern AppState app;
```

```cpp
// esp32-opto: счётчик НАРТИС через оптопорт → облако Waterius, веб-морда,
// прозрачный serial по RFC 2217.
// Дизайн: docs/superpowers/specs/2026-09-17-esp32-opto-firmware-design.md
#include <Arduino.h>

#include "app.h"
#include "port/log.h"
#include "port/opto_bus.h"
#include "port/storage.h"

AppState app;

void setup() {
    Log.begin(115200);
    delay(200);
    Log.printf("esp32-opto %s\n", FIRMWARE_VERSION);

    storage::loadSettings(app.sett);
    app.hasReading = storage::loadLastReading(app.last, app.lastReadAt);
    app.otaError = storage::loadOtaError();

    bus.begin(app.sett.serial);
    Log.printf("Настройки: оптопорт %lu %u%c%u, сеть «%s», период %u мин, чтение в NVS: %s\n",
                  (unsigned long)app.sett.serial.baud, app.sett.serial.bits, app.sett.serial.parity,
                  app.sett.serial.stop, app.sett.ssid, app.sett.periodMin, app.hasReading ? "есть" : "нет");
}

void loop() { delay(100); }
```

- [ ] **Шаг 12: Сборка**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3 -e esp32-c3`
Expected: код возврата 0, в конце `esp32-s3 SUCCESS` и `esp32-c3 SUCCESS`. Первая сборка скачивает библиотеки.

- [ ] **Шаг 13: На железе (владелец)**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3 -t upload && ~/.platformio/penv/bin/pio device monitor`
Expected в логе (метки времени примерные):
```
[0.215] esp32-opto 0.2.0
[0.231] Настройки: оптопорт 9600 8N1, сеть «», период 60 мин, чтение в NVS: нет
```

- [ ] **Шаг 14: Коммит**

```bash
git add -A platformio.ini src
git commit -m "feat: фундамент прошивки — библиотеки, настройки в NVS, арбитр оптопорта, лог"
```

---

## Задача 2: Адаптер НАРТИС на GuruxDLMS.c

**Files:**
- Create: `src/core/nartis.h`, `src/core/nartis.cpp`
- Modify (заменить целиком): `src/main.cpp`
- Modify: `test/test_nartis/meter_emulator.h` (3 правки), `test/test_nartis/test_main.cpp` (4 правки)
- Test: `test/test_nartis` — `~/.platformio/penv/bin/pio test -e native`

**Interfaces:**
- Consumes: `core::IOptoPort` (включая `abortRequested()`), `core::MeterData`, `core::ReadResult`, `core::nowMs()`, `core::sleepMs()`; `bus`, `storage::loadSettings`, `app`, `Log` (задача 1).
- Produces: `core::NartisMeter(IOptoPort&)`: `setAddress(uint8_t)` (0 — перебор 16 → 17), `setPassword(const char*)`, `read(MeterData&, char* error, size_t) -> ReadResult`.

Что важно знать о Gurux (проверено по исходникам форка и юнит-тестами на дампе):
- Порядок обмена повторяет `Arduino_IDE/client/client.ino`: `cl_snrmRequest` → `cl_parseUAResponse` → `cl_aarqRequest` → `cl_parseAAREResponse` → `cl_read` + `cl_getData` → `cl_releaseRequest`, `cl_disconnectRequest`. Сегменты — циклом `reply_isMoreData` + `cl_receiverReady`.
- Отказ в ассоциации приходит разными кодами: на отказ по СТО (`a2 03 02 01 01`, диагностика `0d`) Gurux возвращает `DLMS_ERROR_CODE_AUTHENTICATION_FAILURE`, на другие — `DLMS_ERROR_CODE_REJECTED_PERMAMENT`. Поэтому `AuthRejected` ставится по любому ответу на AARQ, кроме молчания, — проверено тестом.
- Свои умолчания AARQ у Gurux шире, чем у проверенного обмена: conformance и max PDU выставляются вручную, иначе запрос отличается от того, на который счётчик ответил.
- `cl_releaseRequest` (RLRQ) не отправляется: в проверенном обмене сеанс закрывается одним DISC.
- Повторы кадров разрешены только после ассоциации: повторный AARQ — ещё одна попытка пароля из пяти, а повторный SNRM затягивает перебор адресов на 8 секунд.
- Время счётчика Gurux отдаёт без пересчёта по deviation, поэтому разбирается через `gmtime_r`, а не `localtime_r`.
- Атрибут 2 регистра Gurux копирует как есть (`cosem_setRegister`), scaler из атрибута 3 применяется вручную.
- Для строк `cl_updateValue` не вызывается: у Gurux там утечки, строка берётся из `reply.dataValue` (так же обходит latonita).

- [ ] **Шаг 1: Создать `src/core/nartis.h`**

```cpp
// Ядро: адаптер счётчика НАРТИС (СПОДЭС = DLMS/COSEM поверх HDLC) на GuruxDLMS.c.
#pragma once
#include "meter.h"

namespace core {

class NartisMeter : public IMeter {
   public:
    explicit NartisMeter(IOptoPort& port) : port_(port) {}

    // 0 — перебрать 16 (серия 100/300), затем 17 (серия И).
    void setAddress(uint8_t addr) { addr_ = addr; }
    // Пароль LLS. Перебирать пароли нельзя: после 5 неверных попыток
    // счётчик блокирует интерфейсы на сутки.
    void setPassword(const char* pwd);

    ReadResult read(MeterData& out, char* error, size_t errorCap) override;

    // Адрес, на котором счётчик отозвался в последний раз.
    uint8_t foundAddress() const { return found_; }

   private:
    IOptoPort& port_;
    uint8_t addr_ = 0;
    uint8_t found_ = 0;  // адрес, на котором счётчик ответил в прошлый раз
    char pwd_[17] = "111";
};

}  // namespace core
```

- [ ] **Шаг 2: Создать `src/core/nartis.cpp`**

```cpp
#include "nartis.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "client.h"
#include "cosem.h"
#include "gxobjects.h"
#include "variant.h"

namespace core {
namespace {

const uint16_t CLIENT_READER = 32;  // считыватель показаний
const uint32_t WAIT_MS = 2000;      // как в примере Gurux Arduino_IDE/client
const uint8_t RESEND_COUNT = 3;
const unsigned char HDLC_FLAG = 0x7E;
const unsigned char UNIT_WH = 30;

// Один сеанс со счётчиком: настройки Gurux и буфер приёма.
// Порядок обмена — как в Arduino_IDE/client/client.ino из GuruxDLMS.c.
class Session {
   public:
    Session(IOptoPort& port, uint8_t phys, const char* pwd) : port_(port) {
        cl_init(&settings_, 1, CLIENT_READER, cl_getServerAddress(1, phys, 2),
                pwd[0] ? DLMS_AUTHENTICATION_LOW : DLMS_AUTHENTICATION_NONE, pwd[0] ? pwd : NULL,
                DLMS_INTERFACE_TYPE_HDLC);
        // Набор услуг и размер PDU в AARQ — как в обмене, проверенном на
        // счётчике (docs/06-nartis-100-exchange.md, conformance 00 7e 1f,
        // max PDU 1200). Свои умолчания Gurux предлагает шире.
        settings_.proposedConformance = (DLMS_CONFORMANCE)(
            DLMS_CONFORMANCE_PRIORITY_MGMT_SUPPORTED | DLMS_CONFORMANCE_ATTRIBUTE_0_SUPPORTED_WITH_GET |
            DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_GET_OR_READ | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_SET_OR_WRITE |
            DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_ACTION | DLMS_CONFORMANCE_MULTIPLE_REFERENCES |
            DLMS_CONFORMANCE_GET | DLMS_CONFORMANCE_SET | DLMS_CONFORMANCE_SELECTIVE_ACCESS |
            DLMS_CONFORMANCE_EVENT_NOTIFICATION | DLMS_CONFORMANCE_ACTION);
        settings_.maxPduSize = 1200;
        BYTE_BUFFER_INIT(&frame_);
        bb_capacity(&frame_, 256);
    }

    ~Session() {
        bb_clear(&frame_);
        cl_clear(&settings_);
    }

    // SNRM → UA → AARQ → AARE. Возвращает код Gurux, 0 — успех.
    int open() {
        message msg;
        gxReplyData reply;
        mes_init(&msg);
        reply_init(&reply);
        int ret = cl_snrmRequest(&settings_, &msg);
        if (ret == 0) ret = exchange(&msg, &reply);
        if (ret == 0) ret = cl_parseUAResponse(&settings_, &reply.data);
        mes_clear(&msg);
        reply_clear(&reply);
        if (ret != 0) return ret;

        ret = cl_aarqRequest(&settings_, &msg);
        if (ret == 0) ret = exchange(&msg, &reply);
        if (ret == 0) ret = cl_parseAAREResponse(&settings_, &reply.data);
        mes_clear(&msg);
        reply_clear(&reply);
        // Счётчик ответил на AARQ отказом: пароль неверный либо не принят
        // механизм аутентификации. Кодов у Gurux несколько (для LLS обычно
        // DLMS_ERROR_CODE_AUTHENTICATION_FAILURE), поэтому отказом считаем
        // любой ответ, кроме молчания.
        refused_ = ret != 0 && !aborted_ && ret != DLMS_ERROR_CODE_RECEIVE_FAILED;
        if (ret == 0) resend_ = true;  // повторы разрешены только после ассоциации
        return ret;
    }

    // Счётчик отверг ассоциацию: повторять нельзя, 5 попыток — блокировка на сутки.
    bool refused() const { return refused_; }

    // DISC. Ошибки не важны: счётчик сам закроет сеанс по таймауту.
    // RLRQ (release) не шлём: проверенный на счётчике обмен закрывается одним
    // DISC (docs/06-nartis-100-exchange.md), а лишний запрос — лишние 2 секунды
    // ожидания, если счётчик на него не отвечает.
    void close() {
        message msg;
        gxReplyData reply;
        mes_init(&msg);
        reply_init(&reply);
        if (cl_disconnectRequest(&settings_, &msg) == 0) exchange(&msg, &reply);
        mes_clear(&msg);
        reply_clear(&reply);
    }

    // GET атрибута. update — разобрать значение в объект через cl_updateValue.
    // Для строк update не используется: у Gurux там утечки (так же обходит latonita).
    int readAttr(gxObject* obj, unsigned char attr, gxReplyData* reply, bool update) {
        message msg;
        mes_init(&msg);
        int ret = cl_read(&settings_, obj, attr, &msg);
        if (ret == 0) ret = exchange(&msg, reply);
        if (ret == 0 && update) ret = cl_updateValue(&settings_, obj, attr, &reply->dataValue);
        mes_clear(&msg);
        return ret;
    }

    bool aborted() const { return aborted_; }

   private:
    // Отправить все кадры сообщения и собрать ответ вместе с сегментами.
    int exchange(message* msg, gxReplyData* reply) {
        for (int i = 0; i < msg->size; ++i) {
            int ret = sendAndReceive(msg->data[i], reply);
            if (ret != 0) return ret;
            while (reply_isMoreData(reply)) {
                gxByteBuffer rr;
                BYTE_BUFFER_INIT(&rr);
                ret = cl_receiverReady(&settings_, reply->moreData, &rr);
                if (ret == 0) ret = sendAndReceive(&rr, reply);
                bb_clear(&rr);
                if (ret != 0) return ret;
            }
        }
        return 0;
    }

    int sendAndReceive(gxByteBuffer* data, gxReplyData* reply) {
        reply->complete = 0;
        bb_empty(&frame_);
        port_.flushInput();
        port_.write(data->data, data->size);
        uint8_t resend = 0;
        do {
            int ret = readFrame();
            if (ret != 0) {
                // До ассоциации повторов нет: повторный AARQ — ещё одна попытка
                // пароля, а повторный SNRM затягивает перебор адресов.
                if (aborted_ || !resend_ || resend == RESEND_COUNT) return ret;
                ++resend;
                bb_empty(&frame_);
                port_.write(data->data, data->size);
                continue;
            }
            ret = cl_getData(&settings_, &frame_, reply);
            if (ret != 0 && ret != DLMS_ERROR_CODE_FALSE) return ret;
        } while (reply->complete == 0);
        return 0;
    }

    // Дочитать байты до флага 0x7E в конце кадра (com_readSerialPort в примере Gurux).
    int readFrame() {
        uint32_t start = nowMs();
        uint32_t lastIndex = frame_.position;
        while (true) {
            if (port_.abortRequested()) {
                aborted_ = true;
                return DLMS_ERROR_CODE_RECEIVE_FAILED;
            }
            int avail = port_.available();
            if (avail > 0) {
                if (frame_.size + avail > frame_.capacity) bb_capacity(&frame_, 20 + frame_.size + avail);
                for (int i = 0; i < avail; ++i) {
                    int c = port_.read();
                    if (c < 0) break;
                    frame_.data[frame_.size++] = (unsigned char)c;
                }
                if (frame_.size > 5) {
                    for (uint32_t pos = frame_.size - 1; pos != lastIndex; --pos) {
                        if (frame_.data[pos] == HDLC_FLAG) return 0;
                    }
                    lastIndex = frame_.size - 1;
                }
            } else {
                sleepMs(1);  // флаг прерывания проверяется каждую миллисекунду
            }
            if (nowMs() - start >= WAIT_MS) return DLMS_ERROR_CODE_RECEIVE_FAILED;
        }
    }

    IOptoPort& port_;
    dlmsSettings settings_;
    gxByteBuffer frame_;
    bool aborted_ = false;
    bool refused_ = false;
    bool resend_ = false;
};

// Строки НАРТИС (тип счётчика) приходят в cp1251 — переводим в UTF-8 для веба и облака.
void cp1251ToUtf8(const unsigned char* src, size_t len, char* out, size_t cap) {
    size_t k = 0;
    for (size_t i = 0; i < len && src[i]; ++i) {
        unsigned char c = src[i];
        uint16_t u;
        if (c < 0x80) u = c < 0x20 ? '?' : c;
        else if (c >= 0xC0) u = 0x0410 + (c - 0xC0);  // А..я
        else if (c == 0xA8) u = 0x0401;               // Ё
        else if (c == 0xB8) u = 0x0451;               // ё
        else u = '?';
        if (u < 0x80) {
            if (k + 1 >= cap) break;
            out[k++] = (char)u;
        } else {
            if (k + 2 >= cap) break;
            out[k++] = (char)(0xC0 | (u >> 6));
            out[k++] = (char)(0x80 | (u & 0x3F));
        }
    }
    out[k] = 0;
}

// 1.0.1.8.t.255: атрибут 3 — scaler и unit, атрибут 2 — значение как есть.
// Gurux scaler не применяет (cosem_setRegister копирует значение).
int readEnergy(Session& s, uint8_t tariff, double& kwh) {
    char obis[20];
    snprintf(obis, sizeof(obis), "1.0.1.8.%u.255", tariff);
    gxRegister reg;
    gxReplyData reply;
    reply_init(&reply);
    int ret = cosem_init(BASE(reg), DLMS_OBJECT_TYPE_REGISTER, obis);
    if (ret == 0) ret = s.readAttr(BASE(reg), 3, &reply, true);
    reply_clear(&reply);
    if (ret == 0) ret = s.readAttr(BASE(reg), 2, &reply, true);
    if (ret == 0) {
        double v = var_toDouble(&reg.value);
        for (int i = 0; i < reg.scaler; ++i) v *= 10.0;
        for (int i = 0; i > reg.scaler; --i) v /= 10.0;
        if (reg.unit == UNIT_WH) v /= 1000.0;  // Вт·ч → кВт·ч
        kwh = v;
    }
    reply_clear(&reply);
    obj_clear(BASE(reg));
    return ret;
}

void readString(Session& s, const char* obis, char* out, size_t cap) {
    out[0] = 0;
    gxData obj;
    gxReplyData reply;
    reply_init(&reply);
    if (cosem_init(BASE(obj), DLMS_OBJECT_TYPE_DATA, obis) == 0 &&
        s.readAttr(BASE(obj), 2, &reply, false) == 0) {
        gxByteBuffer* bb = NULL;
        if (reply.dataValue.vt == DLMS_DATA_TYPE_OCTET_STRING) bb = reply.dataValue.byteArr;
        if (reply.dataValue.vt == DLMS_DATA_TYPE_STRING) bb = reply.dataValue.strVal;
        if (bb) cp1251ToUtf8(bb->data, bb->size, out, cap);
    }
    reply_clear(&reply);
    obj_clear(BASE(obj));
}

void readClock(Session& s, char* out, size_t cap) {
    out[0] = 0;
    gxClock clk;
    gxReplyData reply;
    reply_init(&reply);
    if (cosem_init(BASE(clk), DLMS_OBJECT_TYPE_CLOCK, "0.0.1.0.0.255") == 0 &&
        s.readAttr(BASE(clk), 2, &reply, true) == 0) {
        // Gurux отдаёт время счётчика как есть, без пересчёта по deviation,
        // поэтому и разбирать его надо без часового пояса устройства.
        time_t t = (time_t)clk.time.value;
        struct tm tm;
        gmtime_r(&t, &tm);
        strftime(out, cap, "%Y-%m-%d %H:%M:%S", &tm);
    }
    reply_clear(&reply);
    obj_clear(BASE(clk));
}

}  // namespace

void NartisMeter::setPassword(const char* pwd) {
    snprintf(pwd_, sizeof(pwd_), "%s", pwd ? pwd : "");
}

ReadResult NartisMeter::read(MeterData& out, char* error, size_t errorCap) {
    out = MeterData();
    error[0] = 0;

    uint8_t candidates[3];
    uint8_t count = 0;
    if (addr_) {
        candidates[count++] = addr_;
    } else {
        if (found_) candidates[count++] = found_;
        if (found_ != 16) candidates[count++] = 16;  // НАРТИС-100/300
        if (found_ != 17) candidates[count++] = 17;  // НАРТИС-И100/И300
    }

    for (uint8_t i = 0; i < count; ++i) {
        uint8_t addr = candidates[i];
        Session s(port_, addr, pwd_);
        int ret = s.open();
        if (s.aborted()) return ReadResult::Aborted;
        if (s.refused()) {
            snprintf(error, errorCap, "счётчик отверг пароль (адрес %u, код %d)", addr, ret);
            return ReadResult::AuthRejected;
        }
        if (ret != 0) {
            // На чужой адрес счётчик не отвечает — пробуем следующий
            snprintf(error, errorCap, "нет связи со счётчиком (адрес %u, код %d)", addr, ret);
            continue;
        }
        found_ = addr;

        ret = readEnergy(s, 0, out.total);
        if (ret == 0) {
            for (uint8_t t = 1; t <= MAX_TARIFFS && !s.aborted(); ++t) {
                double v = 0;
                if (readEnergy(s, t, v) != 0) break;  // тарифы читаются до первого отказа
                out.tariff[t - 1] = v;
                out.tariffCount = t;
            }
            readString(s, "0.0.96.1.0.255", out.serial, sizeof(out.serial));
            readString(s, "0.0.96.1.1.255", out.model, sizeof(out.model));
            readString(s, "0.0.96.1.2.255", out.fwVersion, sizeof(out.fwVersion));
            readClock(s, out.time, sizeof(out.time));
        }
        if (s.aborted()) return ReadResult::Aborted;  // порт уже у клиента — DISC не шлём
        s.close();
        if (ret != 0) {
            snprintf(error, errorCap, "ошибка чтения энергии (код %d)", ret);
            return ReadResult::Failed;
        }
        return ReadResult::Ok;
    }
    return ReadResult::Failed;
}

}  // namespace core
```

- [ ] **Шаг 3: `src/main.cpp` (заменить целиком)**

Временная проверка адаптера: чтение раз в минуту в лог. При `AuthRejected` проверка останавливается до перезагрузки — повторы с неверным паролем заблокировали бы счётчик. В задаче 4 её заменит `poller`.

```cpp
// esp32-opto: счётчик НАРТИС через оптопорт → облако Waterius, веб-морда,
// прозрачный serial по RFC 2217.
// Дизайн: docs/superpowers/specs/2026-09-17-esp32-opto-firmware-design.md
#include <Arduino.h>

#include "app.h"
#include "core/nartis.h"
#include "port/log.h"
#include "port/opto_bus.h"
#include "port/storage.h"

AppState app;

namespace {

// Временная проверка адаптера: чтение раз в минуту в лог. В задаче 4 её заменит poller.
core::NartisMeter testMeter(bus);
const uint32_t TEST_READ_EVERY_MS = 60UL * 1000;
uint32_t testReadAt = 0;
bool testStopped = false;

void testRead() {
    if (testStopped || (testReadAt && millis() - testReadAt < TEST_READ_EVERY_MS)) return;
    testReadAt = millis() | 1;
    if (!bus.acquireForMeter()) return;

    testMeter.setAddress(app.sett.meterAddr);
    testMeter.setPassword(app.sett.meterPwd);
    bus.configure(app.sett.serial);
    core::MeterData data;
    char error[64];
    core::ReadResult result = testMeter.read(data, error, sizeof(error));
    bus.releaseMeter();

    Log.printf("Чтение: результат %d %s\n", (int)result, error);
    if (result == core::ReadResult::AuthRejected) {
        // Повторять нельзя: 5 неверных паролей — блокировка счётчика на сутки
        testStopped = true;
        Log.println("Опрос остановлен до перезагрузки");
        return;
    }
    if (result != core::ReadResult::Ok) return;
    Log.printf("  всего %.3f кВт·ч, sn %s, модель %s, ПО %s, время %s\n", data.total, data.serial,
                  data.model, data.fwVersion, data.time);
    for (uint8_t i = 0; i < data.tariffCount; ++i) Log.printf("  T%u %.3f кВт·ч\n", i + 1, data.tariff[i]);
}

}  // namespace

void setup() {
    Log.begin(115200);
    delay(200);
    Log.printf("esp32-opto %s\n", FIRMWARE_VERSION);

    storage::loadSettings(app.sett);
    app.hasReading = storage::loadLastReading(app.last, app.lastReadAt);
    app.otaError = storage::loadOtaError();

    bus.begin(app.sett.serial);
}

void loop() {
    testRead();
    delay(100);
}
```

- [ ] **Шаг 4: Сборка**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3 -e esp32-c3`
Expected: код возврата 0, оба env `SUCCESS`.

- [ ] **Шаг 5: Тесты под новый адаптер**

Тесты проверяют адаптер снаружи, через байты оптопорта, поэтому меняется только обвязка. Заглушка реального обмена пересобирает ответ счётчика с номерами кадров под запрос: данные в дампе настоящие, а порядок чтения у прошивки не такой, как у скрипта, и номера не совпадают.

Правка 1 в `test/test_nartis/meter_emulator.h` — найти:

```
    void flushInput() override { out_.clear(); }
```

заменить на:

```
    void flushInput() override { out_.clear(); }
    bool abortRequested() override { return false; }
```

Правка 2 в `test/test_nartis/meter_emulator.h` — найти:

```
// Воспроизводит реальный обмен: на запрос отвечает кадром, который прислал
// настоящий счётчик на такой же запрос. Кадры без данных (SNRM, DISC) должны
// совпасть целиком, I-кадры — по данным (LLC + PDU): номера последовательности
// в прошивке другие, потому что порядок чтения не как в скрипте.
```

заменить на:

```
// Воспроизводит реальный обмен: на запрос отвечает данными, которые прислал
// настоящий счётчик на такой же запрос. Кадры без данных (SNRM, DISC) должны
// совпасть целиком, I-кадры — по данным (LLC + PDU): номера последовательности
// в прошивке другие, потому что порядок чтения не как в скрипте. Ответ поэтому
// собирается заново, с номерами под запрос.
```

Правка 3 в `test/test_nartis/meter_emulator.h` — найти:

```
            if (same) {
                send(hex(e.response));
                return;
            }
```

заменить на:

```
            if (!same) continue;
            Frame resp = parseFrame(hex(e.response));
            if (f.info.empty()) {  // SNRM, DISC — ответ как есть
                send(hex(e.response));
            } else {
                uint8_t ns = (uint8_t)((f.control >> 1) & 7);
                uint8_t control = (uint8_t)((((ns + 1) & 7) << 5) | 0x10 | (ns << 1));
                send(serverFrame(f.dst[1] >> 1, control, resp.info, false));
            }
            return;
```

Правка 1 в `test/test_nartis/test_main.cpp` — найти:

```
struct Reading {
    bool ok = false;
    core::MeterData data;
```

заменить на:

```
struct Reading {
    bool ok = false;
    core::ReadResult result = core::ReadResult::Failed;
    core::MeterData data;
```

Правка 2 в `test/test_nartis/test_main.cpp` — найти:

```
    r.ok = meter.read(r.data);
    snprintf(r.error, sizeof(r.error), "%s", r.data.error);
```

заменить на:

```
    r.result = meter.read(r.data, r.error, sizeof(r.error));
    r.ok = r.result == core::ReadResult::Ok;
```

Правка 3 в `test/test_nartis/test_main.cpp` — найти:

```
    TEST_ASSERT_EQUAL(0, meter.gets);
    TEST_ASSERT_TRUE(strlen(r.error) > 0);
```

заменить на:

```
    TEST_ASSERT_EQUAL(0, meter.gets);
    TEST_ASSERT_TRUE(r.result == core::ReadResult::AuthRejected);
    TEST_ASSERT_TRUE(strlen(r.error) > 0);
```

Правка 4 в `test/test_nartis/test_main.cpp` — найти:

```
    meter.password = "12345";
    Reading r = readMeter(meter);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL(1, meter.aarqs);
}

void test_silent_meter
```

заменить на:

```
    meter.password = "12345";
    Reading r = readMeter(meter);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(r.result == core::ReadResult::AuthRejected);
    TEST_ASSERT_EQUAL(1, meter.aarqs);
}

void test_silent_meter
```

- [ ] **Шаг 6: Прогон тестов**

Run: `~/.platformio/penv/bin/pio test -e native`
Expected: код возврата 0, `18 test cases: 18 succeeded`. Первый запуск скачивает Gurux и Unity.

Если падает `test_real_dump_requests_are_byte_identical`, значит Gurux шлёт не те байты, что принял настоящий счётчик. Ожидания из дампа не править: либо настроить Gurux так, чтобы запросы совпали, либо заново снять дамп на счётчике.

- [ ] **Шаг 7: На железе (владелец)**

Головка на оптопорте счётчика, прошить и открыть монитор. Первое чтение — сразу после старта, дальше раз в минуту.
Expected (перед итогом — обмен кадрами HDLC: SNRM на адрес 16 с управляющим байтом `93`, ответ UA — с `73` и адресами в обратном порядке):
```
TX 7e a0 08 02 21 41 93 50 b4 7e
RX 7e a0 .. 41 02 21 73 .. 7e
TX 7e a0 .. (AARQ с паролем)
RX 7e a0 .. (AARE)
...
Чтение: результат 0
  всего 12345.678 кВт·ч, sn 012345678901, модель НАРТИС-100..., ПО ..., время 2026-09-17 12:00:00
  T1 ... кВт·ч
  T2 ... кВт·ч
```
Без головки: только строки `TX 7e a0 08 02 21 41 93 50 b4 7e` (адрес 16) и `TX 7e a0 08 02 23 41 93 e8 01 7e` (адрес 17) без `RX`, затем `Чтение: результат 1 нет связи со счётчиком (адрес 17, код ...)`.

- [ ] **Шаг 8: Коммит**

```bash
git add src/core/nartis.h src/core/nartis.cpp src/main.cpp test/test_nartis
git commit -m "feat: адаптер счётчика НАРТИС на GuruxDLMS.c"
```

---

## Задача 3: Wi-Fi-супервизор, веб-сервер, портал Wi-Fi, страница лога

**Files:**
- Create: `src/port/net.h`, `src/port/net.cpp`, `src/port/wifi_portal.h`, `src/port/wifi_portal.cpp`, `src/port/web.h`, `src/port/web.cpp`, `data/style.css`, `data/app.js`, `data/wifi.html`, `data/log.html`
- Modify (заменить целиком): `src/main.cpp`

**Interfaces:**
- Consumes: `app.sett`, `storage::saveSettings`, `Log.read/bootId`, `LogSink::SIZE` (задача 1).
- Produces:
  - `net::begin/loop/reconnect(const core::Settings&)`, `net::connected()`, `net::apActive()`, `net::status() -> net::Status {Idle, Connecting, Connected, Failed}`, `net::modeName()`, `net::apName()`, `net::ip() -> String`, `net::rssi()`, `net::chipId()`, `net::takeFastConnect(uint8_t& channel, uint8_t bssid[6]) -> bool`, `net::tlsClient() -> WiFiClientSecure&`, `net::postJson(const Settings&, const char* path, const char* body, String& response) -> int`.
  - `wifi_portal::registerRoutes(AsyncWebServer&)`, `wifi_portal::loop()`.
  - `web::begin()`, `web::loop()`.
  - API: `GET /api/networks` → `[{ssid, level 1..4, wifi_channel, bssid, open}]` или `{"scanning":true}`; `POST /api/wifi` (form: `ssid`, `password`, `wifi_channel`, `bssid`) → `{"ok":true}` или `{"errors":{поле: текст}}`; `GET /api/wifi_status` → `{status: idle|connecting|connected|failed, ssid, ip, rssi, mode}`; `GET /api/log?from=N` → `{boot, next, skipped, text}`.
  - JS (`data/app.js`): `$`, `ajax(url, opts, callback)`, `post(url, callback)`, `formSubmit(event, form, action, done)`, `showPW(id)`, `setText(id, text)`, `showOk(id, text)`, `fmtTime(epoch)`, `fmtKwh(v)`, `fmtUptime(s)`, `logPage()`. Ошибка поля показывается в `<p class="error" id="<имя>-error">`, потеря связи — в `<p id="link-lost">`.

Из портала waterius (`ESP8266/src/portal/`, `src/core/wifi.cpp`, `data/static/common.js`) переносится: скан и формат `/api/networks`, разбор канала и BSSID из скрытых полей, быстрый коннект, опрос статуса, обработчики captive portal. Отличия: одна страница вместо мастера из шести, JSON вместо шаблонов `%var%`, без картинок, `ESPAsyncTCP` → `AsyncTCP`.

- [ ] **Шаг 1: Создать `src/port/net.h` и `src/port/net.cpp`**

```cpp
// Порт: Wi-Fi-супервизор и HTTPS-клиент к облаку.
#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>

#include "../core/settings.h"

namespace net {

enum class Status : uint8_t { Idle, Connecting, Connected, Failed };

void begin(const core::Settings& s);
void loop(const core::Settings& s);
// Сеть сменили со страницы /wifi.
void reconnect(const core::Settings& s);

bool connected();
bool apActive();
Status status();
const char* modeName();  // "STA", "AP", "AP+STA"
const char* apName();    // esp32-opto-XXXX — имя точки доступа и hostname
String ip();
int rssi();
uint32_t chipId();

// true один раз после каждого нового подключения: канал и BSSID роутера
// для быстрого коннекта (как в waterius).
bool takeFastConnect(uint8_t& channel, uint8_t bssid[6]);

// Один TLS-клиент на всю прошивку: куча mbedTLS на C3 плохо переносит фрагментацию.
WiFiClientSecure& tlsClient();

// POST JSON на host из настроек + path. HTTP-код или <0 при ошибке соединения.
int postJson(const core::Settings& s, const char* path, const char* body, String& response);

}  // namespace net
```

```cpp
#include "net.h"

#include <HTTPClient.h>
#include <WiFi.h>

#include "log.h"

namespace net {
namespace {

const uint32_t AP_AFTER_MS = 2UL * 60 * 1000;      // нет роутера 2 минуты → точка доступа
const uint32_t AP_RETRY_MS = 60UL * 1000;          // при поднятой AP — попытка раз в минуту
const uint32_t CONNECT_TIMEOUT_MS = 20UL * 1000;   // для статуса на странице /wifi
const uint32_t HTTP_TIMEOUT_MS = 12000;            // как SERVER_TIMEOUT в waterius

WiFiClientSecure tls;
char apName_[24] = "";
bool ap_ = false;
bool wasConnected = false;
bool fastConnectFresh = false;
uint32_t lostSinceMs = 0;
uint32_t lastTryMs = 0;
uint32_t connectStartMs = 0;
Status status_ = Status::Idle;

bool hasBssid(const uint8_t bssid[6]) {
    for (int i = 0; i < 6; ++i)
        if (bssid[i]) return true;
    return false;
}

void beginSta(const core::Settings& s) {
    // Канал и BSSID известны — подключаемся без полного скана (waterius wifi_begin)
    const uint8_t* bssid = s.channel && hasBssid(s.bssid) ? s.bssid : nullptr;
    WiFi.begin(s.ssid, s.pass, bssid ? s.channel : 0, bssid);
    lastTryMs = millis();
}

void startAp(const core::Settings& s) {
    WiFi.mode(s.ssid[0] ? WIFI_AP_STA : WIFI_AP);
    WiFi.setSleep(false);
    // Одно радио на оба режима: канал AP = канал роутера; 0 SDK не принимает (waterius ap_channel)
    uint8_t channel = s.channel >= 1 && s.channel <= 13 ? s.channel : 1;
    WiFi.softAP(apName_, nullptr, channel, 0, 4);
    WiFi.setAutoReconnect(false);  // при поднятой AP переподключаемся сами, раз в минуту
    ap_ = true;
    Log.printf("Wi-Fi: точка доступа %s, http://192.168.4.1\n", apName_);
}

void stopAp() {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    ap_ = false;
    Log.println("Wi-Fi: точка доступа выключена");
}

}  // namespace

void begin(const core::Settings& s) {
    uint64_t mac = ESP.getEfuseMac();
    snprintf(apName_, sizeof(apName_), "esp32-opto-%02X%02X", (unsigned)((mac >> 32) & 0xFF),
             (unsigned)((mac >> 40) & 0xFF));
    tls.setInsecure();  // как в прошивке Waterius: сертификат не проверяем
    WiFi.persistent(false);
    WiFi.setHostname(apName_);
    configTime(0, 0, "ru.pool.ntp.org");

    lostSinceMs = millis();
    if (!s.ssid[0]) {
        startAp(s);
        return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // устройство всегда в сети
    WiFi.setAutoReconnect(true);
    beginSta(s);
    status_ = Status::Connecting;
    connectStartMs = millis();
}

void loop(const core::Settings& s) {
    uint32_t now = millis();

    if (WiFi.status() == WL_CONNECTED) {
        if (!wasConnected) {
            wasConnected = true;
            fastConnectFresh = true;
            status_ = Status::Connected;
            lostSinceMs = 0;
            Log.printf("Wi-Fi: подключено к %s, IP %s\n", WiFi.SSID().c_str(),
                          WiFi.localIP().toString().c_str());
        }
        // AP гасится, только когда к ней никто не подключён
        if (ap_ && WiFi.softAPgetStationNum() == 0) stopAp();
        return;
    }

    if (wasConnected) {
        wasConnected = false;
        lostSinceMs = now;
        Log.println("Wi-Fi: связь с роутером потеряна");
    }
    if (status_ == Status::Connecting && now - connectStartMs >= CONNECT_TIMEOUT_MS) status_ = Status::Failed;

    if (!s.ssid[0]) {
        if (!ap_) startAp(s);
        return;
    }
    if (!ap_ && now - lostSinceMs >= AP_AFTER_MS) startAp(s);
    if (ap_ && now - lastTryMs >= AP_RETRY_MS) {
        WiFi.disconnect();
        beginSta(s);
    }
}

void reconnect(const core::Settings& s) {
    WiFi.mode(ap_ ? WIFI_AP_STA : WIFI_STA);
    WiFi.setSleep(false);
    WiFi.disconnect();
    beginSta(s);
    status_ = Status::Connecting;
    connectStartMs = millis();
    wasConnected = false;
    if (!lostSinceMs) lostSinceMs = millis();
}

bool connected() { return WiFi.status() == WL_CONNECTED; }
bool apActive() { return ap_; }
Status status() { return status_; }

const char* modeName() {
    if (!ap_) return "STA";
    return WiFi.getMode() == WIFI_AP ? "AP" : "AP+STA";
}

const char* apName() { return apName_; }

String ip() {
    if (connected()) return WiFi.localIP().toString();
    if (ap_) return WiFi.softAPIP().toString();
    return String();
}

int rssi() { return connected() ? WiFi.RSSI() : 0; }
uint32_t chipId() { return (uint32_t)(ESP.getEfuseMac() & 0xFFFFFF); }

bool takeFastConnect(uint8_t& channel, uint8_t bssid[6]) {
    if (!fastConnectFresh || !connected()) return false;
    fastConnectFresh = false;
    channel = (uint8_t)WiFi.channel();
    memcpy(bssid, WiFi.BSSID(), 6);
    return true;
}

WiFiClientSecure& tlsClient() { return tls; }

int postJson(const core::Settings& s, const char* path, const char* body, String& response) {
    if (!connected()) return -1;

    String url = String(s.host);
    if (url.endsWith("/")) url.remove(url.length() - 1);
    url += path;

    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    WiFiClient plain;
    bool ok = url.startsWith("https://") ? http.begin(tls, url) : http.begin(plain, url);
    if (!ok) return -2;

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Waterius-Token", s.key);
    http.addHeader("Waterius-Email", s.email);
    int code = http.POST((uint8_t*)body, strlen(body));
    if (code > 0) response = http.getString();
    http.end();
    return code;
}

}  // namespace net
```

- [ ] **Шаг 2: Создать `src/port/wifi_portal.h` и `src/port/wifi_portal.cpp`**

```cpp
// Порт: настройка Wi-Fi — перенос нужной части портала waterius
// (ESP8266/src/portal/active_point*.cpp): скан сетей, подключение, статус,
// captive portal в режиме точки доступа.
#pragma once
#include <ESPAsyncWebServer.h>

namespace wifi_portal {

void registerRoutes(AsyncWebServer& server);
// DNS-перехват при поднятой AP и применение новой сети из формы.
void loop();

}  // namespace wifi_portal
```

```cpp
#include "wifi_portal.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <WiFi.h>

#include <atomic>

#include "../app.h"
#include "log.h"
#include "net.h"
#include "storage.h"

namespace wifi_portal {
namespace {

const char* PORTAL_URL = "http://192.168.4.1/wifi.html";

DNSServer dns;
bool dnsStarted = false;

// Новая сеть из формы: пишет обработчик (поток async_tcp), применяет loop().
struct PendingWifi {
    char ssid[33];
    char pass[65];
    uint8_t channel;
    uint8_t bssid[6];
};
PendingWifi pending;
std::atomic<bool> pendingFlag{false};

// --- разбор скрытых полей формы: waterius ESP8266/src/core/wifi.cpp ---

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Ноль — канал неизвестен, идём полным сканом.
uint8_t parseWifiChannel(const char* value) {
    long channel = atol(value);
    return channel >= 1 && channel <= 13 ? (uint8_t)channel : 0;
}

// aa:bb:cc:dd:ee:ff, aa-bb-... или 12 hex-символов подряд. При ошибке — нули.
bool parseBssid(const char* value, uint8_t out[6]) {
    memset(out, 0, 6);
    uint8_t bytes[6] = {0};
    int digits = 0;
    for (size_t i = 0; value[i]; ++i) {
        char c = value[i];
        if (c == ':' || c == '-') continue;
        int d = hexDigit(c);
        if (d < 0 || digits >= 12) return false;
        bytes[digits / 2] = (uint8_t)(bytes[digits / 2] << 4 | d);
        digits++;
    }
    if (digits != 12) return false;
    memcpy(out, bytes, 6);
    return true;
}

bool hasBssid(const uint8_t bssid[6]) {
    for (int i = 0; i < 6; ++i)
        if (bssid[i]) return true;
    return false;
}

// --- обработчики ---

void sendJson(AsyncWebServerRequest* request, JsonDocument& doc) {
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

String param(AsyncWebServerRequest* request, const char* name) {
    return request->hasParam(name, true) ? request->getParam(name, true)->value() : String();
}

// Список сетей (waterius get_api_networks). Скан асинхронный: пока идёт,
// отвечаем {"scanning":true}, страница переспрашивает.
void getNetworks(AsyncWebServerRequest* request) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
        request->send(200, "application/json", "{\"scanning\":true}");
        return;
    }
    if (n < 0) {
        WiFi.scanNetworks(true);
        request->send(200, "application/json", "{\"scanning\":true}");
        return;
    }
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < n; ++i) {
        JsonObject o = arr.add<JsonObject>();
        o["ssid"] = WiFi.SSID(i);
        long level = map(WiFi.RSSI(i), -100, -50, 1, 4);
        o["level"] = level < 1 ? 1 : (level > 4 ? 4 : level);
        // Канал и BSSID именно этой сети: форма вернёт их, и коннект пойдёт без скана
        o["wifi_channel"] = WiFi.channel(i);
        o["bssid"] = WiFi.BSSIDstr(i);
        o["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();
    sendJson(request, doc);
}

// Новая сеть (waterius post_api_save_connect + save_fast_connect).
void postWifi(AsyncWebServerRequest* request) {
    JsonDocument doc;
    JsonObject errors = doc["errors"].to<JsonObject>();
    String ssid = param(request, "ssid");
    String pass = param(request, "password");
    if (ssid.isEmpty() || ssid.length() > 32) errors["ssid"] = "Введите название сети, до 32 символов";
    if (pass.length() > 64 || (pass.length() > 0 && pass.length() < 8))
        errors["password"] = "Пароль — от 8 до 64 символов, пусто для открытой сети";
    if (errors.size()) {
        sendJson(request, doc);
        return;
    }

    PendingWifi p = {};
    snprintf(p.ssid, sizeof(p.ssid), "%s", ssid.c_str());
    snprintf(p.pass, sizeof(p.pass), "%s", pass.c_str());
    // Пара пишется целиком или не пишется: канал без BSSID хуже полного скана
    p.channel = parseWifiChannel(param(request, "wifi_channel").c_str());
    if (!p.channel || !parseBssid(param(request, "bssid").c_str(), p.bssid) || !hasBssid(p.bssid)) {
        p.channel = 0;
        memset(p.bssid, 0, sizeof(p.bssid));
    }
    pending = p;
    pendingFlag.store(true);

    doc.remove("errors");
    doc["ok"] = true;
    sendJson(request, doc);
}

void getWifiStatus(AsyncWebServerRequest* request) {
    static const char* NAMES[] = {"idle", "connecting", "connected", "failed"};
    JsonDocument doc;
    doc["status"] = NAMES[(int)net::status()];
    doc["ssid"] = app.sett.ssid;
    doc["ip"] = net::ip();
    doc["rssi"] = net::rssi();
    doc["mode"] = net::modeName();
    sendJson(request, doc);
}

void redirectToPortal(AsyncWebServerRequest* request) { request->redirect(PORTAL_URL); }

}  // namespace

void registerRoutes(AsyncWebServer& server) {
    server.on("/api/networks", HTTP_GET, getNetworks);
    server.on("/api/wifi", HTTP_POST, postWifi);
    server.on("/api/wifi_status", HTTP_GET, getWifiStatus);

    // Captive portal — проверки ОС из waterius active_point.cpp, только для клиентов AP
    server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("http://logout.net");  // обход для Windows 11
    }).setFilter(ON_AP_FILTER);
    server.on("/wpad.dat", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(404);  // иначе Windows 10 спрашивает его бесконечно
    }).setFilter(ON_AP_FILTER);
    server.on("/generate_204", HTTP_GET, redirectToPortal).setFilter(ON_AP_FILTER);        // Android
    server.on("/redirect", HTTP_GET, redirectToPortal).setFilter(ON_AP_FILTER);            // Microsoft
    server.on("/hotspot-detect.html", HTTP_GET, redirectToPortal).setFilter(ON_AP_FILTER); // Apple
    server.on("/canonical.html", HTTP_GET, redirectToPortal).setFilter(ON_AP_FILTER);      // Firefox
    server.on("/success.txt", HTTP_GET, redirectToPortal).setFilter(ON_AP_FILTER);         // Firefox
    server.on("/ncsi.txt", HTTP_GET, redirectToPortal).setFilter(ON_AP_FILTER);            // Windows
    server.on("/fwlink", HTTP_GET, redirectToPortal).setFilter(ON_AP_FILTER);              // Microsoft
}

void loop() {
    if (net::apActive() && !dnsStarted) {
        dns.start(53, "*", WiFi.softAPIP());
        dnsStarted = true;
    } else if (!net::apActive() && dnsStarted) {
        dns.stop();
        dnsStarted = false;
    }
    if (dnsStarted) dns.processNextRequest();

    if (pendingFlag.exchange(false)) {
        PendingWifi p = pending;
        memcpy(app.sett.ssid, p.ssid, sizeof(app.sett.ssid));
        memcpy(app.sett.pass, p.pass, sizeof(app.sett.pass));
        memcpy(app.sett.bssid, p.bssid, sizeof(app.sett.bssid));
        app.sett.channel = p.channel;
        storage::saveSettings(app.sett);
        Log.printf("Wi-Fi: новая сеть %s\n", app.sett.ssid);
        net::reconnect(app.sett);
    }
}

}  // namespace wifi_portal
```

- [ ] **Шаг 3: Создать `src/port/web.h` и `src/port/web.cpp`**

`web.cpp` на этом шаге отдаёт статику, маршруты Wi-Fi и `/api/log`; API статуса и настроек добавит задача 4. Текст лога уходит в JSON как `const char*` — ArduinoJson не копирует его в документ.

```cpp
// Порт: веб-сервер. Страницы — статика из LittleFS (data/), данные — JSON API.
// Обработчики выполняются в задаче async_tcp: они только читают состояние
// и ставят флаги, всё остальное делает loop().
#pragma once

namespace web {
void begin();
void loop();
}  // namespace web
```

```cpp
#include "web.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include <memory>

#include "log.h"
#include "wifi_portal.h"

namespace web {
namespace {

AsyncWebServer server(80);

// Текст лога после позиции from; страница log.html опрашивает раз в секунду.
void getLog(AsyncWebServerRequest* request) {
    uint32_t from = request->hasParam("from") ? strtoul(request->getParam("from")->value().c_str(), nullptr, 10) : 0;
    std::unique_ptr<char[]> text(new char[LogSink::SIZE + 1]);
    size_t len = 0;
    bool skipped = false;
    uint32_t next = Log.read(from, text.get(), LogSink::SIZE, len, skipped);
    text[len] = 0;

    JsonDocument doc;
    doc["boot"] = Log.bootId();
    doc["next"] = next;
    doc["skipped"] = skipped;
    doc["text"] = (const char*)text.get();  // const char* — без копии в документ
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

}  // namespace

void begin() {
    if (!LittleFS.begin()) Log.println("LittleFS не смонтирован: залейте образ командой uploadfs");

    server.on("/api/log", HTTP_GET, getLog);
    wifi_portal::registerRoutes(server);

    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("no-cache");
    server.onNotFound([](AsyncWebServerRequest* request) {
        // Клиент точки доступа открыл чужой адрес — ведём в настройку Wi-Fi
        if (ON_AP_FILTER(request)) request->redirect("http://192.168.4.1/wifi.html");
        else request->send(404, "text/plain", "Not found");
    });
    server.begin();
}

void loop() {}

}  // namespace web
```

- [ ] **Шаг 4: Создать `data/style.css`, `data/app.js`, `data/wifi.html`, `data/log.html`**

`app.js` на этом шаге — общая часть и разделы Wi-Fi и лога; разделы статуса и настроек добавит задача 4. Страница лога опрашивает `/api/log` своим циклом на `fetch`, а не через `ajax()`: `ajax()` после трёх неудач перестаёт вызывать колбэк, и опрос бы остановился.

```css
/* Цвета и формы — по мотивам портала waterius (ESP8266/data/static/style.css) */
*{box-sizing:border-box;margin:0;padding:0;font-family:Roboto,Helvetica,Arial,sans-serif}
body{background:#fff;color:#232323}
.wrap{max-width:420px;margin:0 auto;padding:16px}
.nav{display:flex;flex-wrap:wrap;gap:16px;margin-bottom:8px}
.nav a{color:#1655F5;text-decoration:none;font-weight:700}
.nav a.on{color:#232323}
h2{font-size:22px;font-weight:700;margin:24px 0 12px}
table{width:100%;border-collapse:collapse}
td{padding:6px 0;border-bottom:1px solid #eee;vertical-align:top;line-height:20px}
td:last-child{text-align:right;padding-left:12px;word-break:break-word}
label{display:block;margin:12px 0 4px;font-size:14px}
input,select{width:100%;height:44px;padding:0 12px;border:1px solid #ccc;border-radius:8px;font-size:16px;background:#fff}
.chk{display:flex;align-items:center;gap:8px;margin:12px 0}
.chk input{width:22px;height:22px}
.chk label{margin:0;font-size:16px}
.pw{position:relative}
.pw input{padding-right:96px}
.pw button{position:absolute;right:6px;top:6px;height:32px;padding:0 8px;border:0;background:none;color:#1655F5;font-size:14px}
.btn{width:100%;height:48px;margin-top:16px;border:0;border-radius:8px;background:#1655F5;color:#fff;font-size:16px}
.btn:disabled{background:#9db4f0}
.btn-serv{width:100%;height:44px;margin-top:12px;border:1px solid #1655F5;border-radius:8px;background:#fff;color:#1655F5;font-size:16px}
.error{color:#F53410;font-size:14px;margin-top:4px}
.form-error{color:#F53410;background:#FFF0ED;padding:12px;border-radius:8px;margin:8px 0}
.ok{color:#1a7f37;margin-top:12px;line-height:20px}
.text{line-height:20px;margin:8px 0}
.net{display:flex;justify-content:space-between;gap:12px;padding:12px 0;border-bottom:1px solid #eee;cursor:pointer}
.lvl{color:#1655F5;white-space:nowrap}
.wrap.wide{max-width:960px}
.log{height:65vh;overflow:auto;margin-top:8px;padding:8px;border:1px solid #ccc;border-radius:8px;background:#f7f7f7;font:12px/16px Menlo,Consolas,monospace;white-space:pre-wrap;word-break:break-all}
.hd{display:none}
```

```js
// Общий код четырёх страниц. Запросы с повторами, отправка форм и список сетей —
// по мотивам портала waterius (ESP8266/data/static/common.js).

const AJAX_TRIES = 3;
const AJAX_RETRY_MS = 1000;

function $(id) { return document.getElementById(id); }

function ajax(url, opts, callback, _try = 0) {
    fetch(url, opts)
        .then(res => res.ok ? res.text() : Promise.reject(res))
        .then(text => {
            linkLost(false);
            let data = text;
            try { data = JSON.parse(text); } catch (e) {}
            callback(data);
        })
        .catch(err => {
            if (++_try < AJAX_TRIES) {
                setTimeout(() => ajax(url, opts, callback, _try), AJAX_RETRY_MS);
                return;
            }
            // Ответ с кодом — ошибка прошивки; ответа нет вовсе — устройство недоступно
            if (err && err.status !== undefined) alert('Ошибка ' + err.status);
            else linkLost(true);
        });
}

function linkLost(lost) {
    const box = $('link-lost');
    if (box) box.classList.toggle('hd', !lost);
}

function post(url, callback) {
    ajax(url, { method: 'POST' }, callback);
}

// Как formSubmit в waterius: чекбокс уходит как 1/0, ошибки полей — в <p id="имя-error">
function formSubmit(event, form, action, done) {
    event.preventDefault();
    const data = new URLSearchParams();
    form.querySelectorAll('input,select').forEach(inp => {
        if (!inp.name) return;
        if (inp.type == 'checkbox') return data.append(inp.name, inp.checked ? 1 : 0);
        data.append(inp.name, inp.value.trim());
    });
    form.querySelectorAll('p.error').forEach(p => p.classList.add('hd'));
    ajax(action, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: data
    }, res => {
        if (res.errors && Object.keys(res.errors).length) {
            for (const k in res.errors) {
                const el = $(k + '-error');
                if (!el) continue;
                el.textContent = res.errors[k];
                el.classList.remove('hd');
            }
            return;
        }
        if (done) done(res);
    });
}

function showPW(id) {
    const pw = $(id);
    pw.type = pw.type == 'password' ? 'text' : 'password';
}

function setText(id, text) {
    const el = $(id);
    if (el) el.textContent = text;
}

function showOk(id, text) {
    setText(id, text);
    $(id).classList.remove('hd');
}

function fmtTime(epoch) {
    return epoch ? new Date(epoch * 1000).toLocaleString('ru-RU') : 'время неизвестно';
}

function fmtKwh(v) { return Number(v).toFixed(3); }

function fmtUptime(s) {
    const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.floor(s % 3600 / 60);
    return (d ? d + ' д ' : '') + h + ' ч ' + m + ' мин';
}

/* ---------- Wi-Fi ---------- */

const WIFI_STATUS = {
    idle: 'не подключено',
    connecting: 'подключение…',
    connected: 'подключено',
    failed: 'не удалось подключиться'
};

function wifiPage() {
    $('ssid').addEventListener('input', () => {
        // Имя введено вручную — канала и BSSID нет, будет полный скан (как в waterius)
        $('wifi_channel').value = '';
        $('bssid').value = '';
    });
    loadNetworks();
    loadWifiStatus();
    setInterval(loadWifiStatus, 3000);
}

function loadWifiStatus() {
    ajax('/api/wifi_status', {}, s => {
        setText('wifi-status', WIFI_STATUS[s.status] || s.status);
        setText('wifi-ssid', s.ssid || '—');
        setText('wifi-ip', s.ip || '—');
        setText('wifi-rssi', s.status == 'connected' ? s.rssi + ' дБм' : '—');
        setText('wifi-mode', s.mode);
    });
}

function loadNetworks() {
    $('networks').innerHTML = '<p class="text">Поиск сетей…</p>';
    ajax('/api/networks', {}, data => {
        if (data.scanning) return setTimeout(loadNetworks, 1500);
        renderNetworks(data);
    });
}

function renderNetworks(list) {
    const box = $('networks');
    box.innerHTML = '';
    if (!list.length) {
        box.innerHTML = '<p class="text">Сети не найдены</p>';
        return;
    }
    list.sort((a, b) => b.level - a.level).forEach(n => {
        // Имя сети — через textContent: оно приходит из эфира
        const row = document.createElement('div');
        row.className = 'net';
        const name = document.createElement('span');
        name.textContent = n.ssid || '(скрытая сеть)';
        const level = document.createElement('span');
        level.className = 'lvl';
        level.textContent = '▮'.repeat(n.level) + '▯'.repeat(4 - n.level) + (n.open ? ' открытая' : '');
        row.appendChild(name);
        row.appendChild(level);
        row.onclick = () => {
            $('ssid').value = n.ssid;
            $('wifi_channel').value = n.wifi_channel;
            $('bssid').value = n.bssid;
            $('password').focus();
        };
        box.appendChild(row);
    });
}

function saveWifi(event, form) {
    $('wifi-result').classList.add('hd');
    formSubmit(event, form, '/api/wifi', () => {
        showOk('wifi-result', 'Подключаюсь… Если связь с устройством пропала — снова подключитесь к его сети Wi-Fi.');
    });
}

/* ---------- Лог ---------- */

const LOG_POLL_MS = 1000;
const LOG_MAX_CHARS = 200000;  // дальше старый текст на экране обрезается

let logFrom = 0;
let logBoot = null;

function logPage() {
    loadLog();
}

// Свой цикл вместо ajax(): при обрыве связи опрос не должен останавливаться
function loadLog() {
    fetch('/api/log?from=' + logFrom)
        .then(res => res.ok ? res.json() : Promise.reject(res))
        .then(d => {
            linkLost(false);
            if (logBoot !== null && d.boot !== logBoot) {
                // Устройство перезагрузилось: позиция from относилась к прошлой загрузке
                logBoot = d.boot;
                logFrom = 0;
                logAppend('\n--- устройство перезагрузилось ---\n');
                return;
            }
            logBoot = d.boot;
            if (d.skipped) logAppend('\n--- часть лога пропущена: буфер переполнился ---\n');
            logAppend(d.text);
            logFrom = d.next;
        })
        .catch(() => linkLost(true))
        .finally(() => setTimeout(loadLog, LOG_POLL_MS));
}

function logAppend(text) {
    if (!text) return;
    const box = $('log');
    box.textContent = (box.textContent + text.replace(/\r/g, '')).slice(-LOG_MAX_CHARS);
    if ($('log-follow').checked) box.scrollTop = box.scrollHeight;
}
```

```html
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>esp32-opto — Wi-Fi</title>
    <link rel="stylesheet" href="/style.css">
    <script src="/app.js"></script>
</head>
<body onload="wifiPage()">
<div class="wrap">
    <nav class="nav">
        <a href="/">Статус</a>
        <a href="/settings.html">Настройки</a>
        <a class="on" href="/wifi.html">Wi-Fi</a>
        <a href="/log.html">Лог</a>
        <a href="/update">Обновление</a>
    </nav>
    <p id="link-lost" class="form-error hd">Нет связи с устройством</p>

    <h2>Подключение</h2>
    <table>
        <tr><td>Состояние</td><td id="wifi-status">…</td></tr>
        <tr><td>Сеть</td><td id="wifi-ssid">—</td></tr>
        <tr><td>IP</td><td id="wifi-ip">—</td></tr>
        <tr><td>Сигнал</td><td id="wifi-rssi">—</td></tr>
        <tr><td>Режим</td><td id="wifi-mode">—</td></tr>
    </table>

    <h2>Выберите сеть</h2>
    <p class="text">Сеть Wi-Fi 2,4 ГГц, через которую устройство будет работать.</p>
    <div id="networks"></div>
    <button class="btn-serv" type="button" onclick="loadNetworks()">Обновить список</button>

    <form onsubmit="saveWifi(event, this)">
        <label for="ssid">Название сети</label>
        <input id="ssid" name="ssid" maxlength="32" required>
        <p class="error hd" id="ssid-error"></p>
        <label for="password">Пароль</label>
        <div class="pw">
            <input id="password" name="password" type="password" maxlength="64" autocomplete="off">
            <button type="button" onclick="showPW('password')">показать</button>
        </div>
        <p class="error hd" id="password-error"></p>
        <input type="hidden" id="wifi_channel" name="wifi_channel">
        <input type="hidden" id="bssid" name="bssid">
        <p class="text">Пока устройство подключается к роутеру, телефон может от него отключиться. Тогда снова выберите сеть устройства в списке Wi-Fi.</p>
        <button class="btn" type="submit">Подключиться</button>
        <p class="ok hd" id="wifi-result"></p>
    </form>
</div>
</body>
</html>
```

```html
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>esp32-opto — лог</title>
    <link rel="stylesheet" href="/style.css">
    <script src="/app.js"></script>
</head>
<body onload="logPage()">
<div class="wrap wide">
    <nav class="nav">
        <a href="/">Статус</a>
        <a href="/settings.html">Настройки</a>
        <a href="/wifi.html">Wi-Fi</a>
        <a class="on" href="/log.html">Лог</a>
        <a href="/update">Обновление</a>
    </nav>
    <p id="link-lost" class="form-error hd">Нет связи с устройством</p>

    <h2>Лог</h2>
    <p class="text">Сообщения прошивки и байты оптопорта (TX — в счётчик, RX — от счётчика) в реальном времени. Устройство хранит последние 16 КБ — примерно 250 строк.</p>
    <div class="chk">
        <input type="checkbox" id="log-follow" checked>
        <label for="log-follow">Автопрокрутка</label>
    </div>
    <pre id="log" class="log"></pre>
    <button class="btn-serv" type="button" onclick="$('log').textContent = ''">Очистить экран</button>
</div>
</body>
</html>
```

- [ ] **Шаг 5: `src/main.cpp` (заменить целиком)**

```cpp
// esp32-opto: счётчик НАРТИС через оптопорт → облако Waterius, веб-морда,
// прозрачный serial по RFC 2217.
// Дизайн: docs/superpowers/specs/2026-09-17-esp32-opto-firmware-design.md
#include <Arduino.h>

#include "app.h"
#include "core/nartis.h"
#include "port/log.h"
#include "port/net.h"
#include "port/opto_bus.h"
#include "port/storage.h"
#include "port/web.h"
#include "port/wifi_portal.h"

AppState app;

namespace {

// Временная проверка адаптера: чтение раз в минуту в лог. В задаче 4 её заменит poller.
core::NartisMeter testMeter(bus);
const uint32_t TEST_READ_EVERY_MS = 60UL * 1000;
uint32_t testReadAt = 0;
bool testStopped = false;

void testRead() {
    if (testStopped || (testReadAt && millis() - testReadAt < TEST_READ_EVERY_MS)) return;
    testReadAt = millis() | 1;
    if (!bus.acquireForMeter()) return;

    testMeter.setAddress(app.sett.meterAddr);
    testMeter.setPassword(app.sett.meterPwd);
    bus.configure(app.sett.serial);
    core::MeterData data;
    char error[64];
    core::ReadResult result = testMeter.read(data, error, sizeof(error));
    bus.releaseMeter();

    Log.printf("Чтение: результат %d %s\n", (int)result, error);
    if (result == core::ReadResult::AuthRejected) {
        // Повторять нельзя: 5 неверных паролей — блокировка счётчика на сутки
        testStopped = true;
        Log.println("Опрос остановлен до перезагрузки");
        return;
    }
    if (result != core::ReadResult::Ok) return;
    Log.printf("  всего %.3f кВт·ч, sn %s, модель %s, ПО %s, время %s\n", data.total, data.serial,
                  data.model, data.fwVersion, data.time);
    for (uint8_t i = 0; i < data.tariffCount; ++i) Log.printf("  T%u %.3f кВт·ч\n", i + 1, data.tariff[i]);
}

// Канал и BSSID роутера после подключения — для быстрого коннекта (как в waterius).
void saveFastConnect() {
    uint8_t channel = 0;
    uint8_t bssid[6];
    if (!net::takeFastConnect(channel, bssid)) return;
    if (channel == app.sett.channel && memcmp(bssid, app.sett.bssid, sizeof(bssid)) == 0) return;
    app.sett.channel = channel;
    memcpy(app.sett.bssid, bssid, sizeof(bssid));
    storage::saveSettings(app.sett);
}

}  // namespace

void setup() {
    Log.begin(115200);
    delay(200);
    Log.printf("esp32-opto %s\n", FIRMWARE_VERSION);

    storage::loadSettings(app.sett);
    app.hasReading = storage::loadLastReading(app.last, app.lastReadAt);
    app.otaError = storage::loadOtaError();

    bus.begin(app.sett.serial);
    net::begin(app.sett);
    web::begin();
}

void loop() {
    net::loop(app.sett);
    saveFastConnect();
    wifi_portal::loop();
    web::loop();
    testRead();
    delay(2);
}
```

- [ ] **Шаг 6: Сборка прошивки и образа ФС**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3 -e esp32-c3`
Expected: код возврата 0, оба env `SUCCESS`.

Run: `~/.platformio/penv/bin/pio run -e esp32-c3 -t buildfs`
Expected: код возврата 0 — образ LittleFS влезает в 128 КБ раздела C3.

- [ ] **Шаг 7: На железе (владелец)**

1. `~/.platformio/penv/bin/pio run -e esp32-s3 -t uploadfs`, затем `-t upload`. Сеть в NVS не сохранена → в логе `Wi-Fi: точка доступа esp32-opto-XXXX, http://192.168.4.1`.
2. Подключить телефон к `esp32-opto-XXXX` → ОС сама открывает страницу `wifi.html` (captive portal). Если не открылась — `http://192.168.4.1/wifi.html`.
3. Список сетей появляется за несколько секунд; нажать на сеть роутера, ввести пароль, «Подключиться» → в логе `Wi-Fi: новая сеть …`, затем `Wi-Fi: подключено к …, IP 192.168.x.y`.
4. Отключить телефон от точки доступа → в логе `Wi-Fi: точка доступа выключена`.
5. С компьютера в той же сети открыть `http://192.168.x.y/wifi.html` → состояние «подключено», режим `STA`.
6. Выключить роутер больше чем на 2 минуты → точка доступа снова появляется; включить роутер → в течение минуты устройство подключается.
7. Открыть `http://192.168.x.y/log.html` → в окне строки с начала загрузки, включая `[…] esp32-opto 0.2.0` и `Wi-Fi: подключено к …`. Нажать RST на плате → через несколько секунд в окне `--- устройство перезагрузилось ---` и новые строки старта, страницу обновлять не нужно. Во время перезагрузки вверху появляется «Нет связи с устройством» и затем пропадает.

- [ ] **Шаг 8: Коммит**

```bash
git add src/port/net.h src/port/net.cpp src/port/wifi_portal.h src/port/wifi_portal.cpp \
        src/port/web.h src/port/web.cpp data src/main.cpp
git commit -m "feat: Wi-Fi-супервизор, веб-сервер и портал Wi-Fi из waterius"
```

---

## Задача 4: Автомат опроса, облако, страницы статуса и настроек

**Files:**
- Create: `src/poller.h`, `src/poller.cpp`, `data/index.html`, `data/settings.html`, `tools/fake_cloud.py`
- Modify: `src/port/web.cpp` (заменить целиком), `data/app.js` (вставка), `src/main.cpp` (заменить целиком)

**Interfaces:**
- Consumes: `core::NartisMeter` (задача 2); `bus`, `storage::*`, `core::buildCloudPayload`, `app` (задача 1); `net::postJson/ip/rssi/chipId`, `web`, JS-хелперы (задача 3).
- Produces:
  - `poller::begin()`, `poller::loop()`, `poller::onMeterEnabled()`, `poller::secondsToNextSend() -> uint32_t`.
  - API: `GET /api/status` → `{fw, ip, rssi, uptime_s, heap, wifi_mode, meter_enabled, meter_reading, meter_error, has_reading, read_at, serial, model, meter_fw, meter_time, total, tariffs[], cloud_at, cloud_code, cloud_error, cloud_next_s}`; `GET /api/settings` → `{baud, bits, parity, stop, meter_enabled, meter_addr, meter_pwd, period_min, host, key, email, rfc_enabled, rfc_port}`; `POST /api/settings` (те же поля формой, чекбоксы `1`/`0`) → `{"ok":true,"reboot":bool}` или `{"errors":{…}}`; `POST /api/read`, `POST /api/send`, `POST /api/reboot` → `{"ok":true}`.

Поведение автомата (спека, раздел 3):
- «Прочитать сейчас» — только чтение. «Отправить сейчас» и период — чтение, если опрос «в работе», затем отправка **последних успешно прочитанных** данных — даже если чтение не удалось, опрос выключен или порт занят прозрачной сессией.
- Ошибка чтения → повтор чтения через 5 минут без отправки. Ошибка отправки → повтор отправки через 5 минут.
- `AuthRejected` → `meterEnabled = false`, сохраняется в NVS, причина в статусе.

- [ ] **Шаг 1: Создать `src/poller.h` и `src/poller.cpp`**

```cpp
// Автомат опроса счётчика и отправки в облако. Работает только из loop().
#pragma once
#include <stdint.h>

namespace poller {

void begin();
void loop();

// Пользователь снова включил опрос тумблером «в работе».
void onMeterEnabled();

// Через сколько секунд очередная отправка по периоду.
uint32_t secondsToNextSend();

}  // namespace poller
```

```cpp
#include "poller.h"

#include <Arduino.h>
#include <time.h>

#include "app.h"
#include "core/cloud.h"
#include "core/nartis.h"
#include "port/log.h"
#include "port/net.h"
#include "port/opto_bus.h"
#include "port/storage.h"

namespace poller {
namespace {

const uint32_t RETRY_MS = 5UL * 60 * 1000;
// Первый цикл вскоре после старта, чтобы после перепрошивки не ждать целый период.
const uint32_t FIRST_CYCLE_MS = 30UL * 1000;

core::NartisMeter meter(bus);

uint32_t periodStartMs = 0;
bool readRetry = false;
uint32_t readRetryStartMs = 0;
bool sendRetry = false;
uint32_t sendRetryStartMs = 0;

uint32_t periodMs() { return (uint32_t)app.sett.periodMin * 60000UL; }

// UTC epoch или 0, пока NTP не синхронизировался.
uint32_t epochNow() {
    time_t t = time(nullptr);
    return t > 1600000000 ? (uint32_t)t : 0;
}

void readMeter() {
    readRetry = false;
    if (!app.sett.meterEnabled) return;
    if (!bus.acquireForMeter()) return;  // порт у прозрачной сессии

    meter.setAddress(app.sett.meterAddr);
    meter.setPassword(app.sett.meterPwd);
    bus.configure(app.sett.serial);

    app.meterReading.store(true);
    core::MeterData data;
    char error[sizeof(app.meterError)];
    core::ReadResult result = meter.read(data, error, sizeof(error));
    app.meterReading.store(false);
    bus.releaseMeter();

    switch (result) {
        case core::ReadResult::Ok:
            app.last = data;
            app.lastReadAt = epochNow();
            app.hasReading = true;
            app.meterError[0] = 0;
            storage::saveLastReading(app.last, app.lastReadAt);
            Log.printf("Счётчик: всего %.3f кВт·ч, тарифов %u, sn %s\n", data.total, data.tariffCount,
                          data.serial);
            break;
        case core::ReadResult::Failed:
            snprintf(app.meterError, sizeof(app.meterError), "%s", error);
            readRetry = true;
            readRetryStartMs = millis();
            Log.printf("Счётчик: %s, повтор через 5 минут\n", error);
            break;
        case core::ReadResult::AuthRejected:
            // После 5 неверных паролей счётчик блокируется на сутки — опрос выключаем
            snprintf(app.meterError, sizeof(app.meterError), "%s", error);
            app.sett.meterEnabled = false;
            storage::saveSettings(app.sett);
            Log.printf("Счётчик: %s, опрос выключен\n", error);
            break;
        case core::ReadResult::Aborted:
            snprintf(app.meterError, sizeof(app.meterError), "чтение прервано прозрачной сессией");
            Log.println("Счётчик: чтение прервано прозрачной сессией");
            break;
    }
}

void sendCloud() {
    sendRetry = false;
    if (!app.hasReading) {
        snprintf(app.cloudError, sizeof(app.cloudError), "нет данных счётчика");
        return;
    }
    if (!app.sett.key[0]) {
        snprintf(app.cloudError, sizeof(app.cloudError), "ключ облака не задан");
        return;
    }

    char body[768];
    String ip = net::ip();
    core::DeviceInfo dev;
    dev.fw = FIRMWARE_VERSION;
    dev.ip = ip.c_str();
    dev.rssi = net::rssi();
    dev.chipId = net::chipId();
    dev.otaError = app.otaError;
    if (!core::buildCloudPayload(app.last, app.lastReadAt, app.sett, dev, body, sizeof(body))) {
        snprintf(app.cloudError, sizeof(app.cloudError), "запрос не собрался");
        return;
    }

    String response;
    int code = net::postJson(app.sett, "/api/source/iz/", body, response);
    app.cloudCode = code;
    Log.printf("Облако: HTTP %d %s\n", code, response.c_str());
    if (code != 200) {
        snprintf(app.cloudError, sizeof(app.cloudError), code < 0 ? "нет соединения" : "сервер ответил ошибкой");
        sendRetry = true;
        sendRetryStartMs = millis();
        return;
    }

    app.cloudAt = epochNow();
    app.cloudError[0] = 0;
    if (app.otaError) {  // ошибка OTA доставлена — обнуляем
        app.otaError = 0;
        storage::saveOtaError(0);
    }
}

}  // namespace

void begin() {
    // Беззнаковая арифметика: now - periodStartMs = periodMs - FIRST_CYCLE_MS
    periodStartMs = millis() - periodMs() + FIRST_CYCLE_MS;
}

void loop() {
    uint32_t now = millis();

    if (app.readNow.exchange(false)) {
        readMeter();
        return;
    }
    // Выход на связь — всегда чтение, затем отправка последних прочитанных данных,
    // даже если чтение не удалось или опрос выключен
    if (app.sendNow.exchange(false)) {
        readMeter();
        sendCloud();
        return;
    }
    if (now - periodStartMs >= periodMs()) {
        periodStartMs = now;
        readMeter();
        sendCloud();
        return;
    }
    if (sendRetry && now - sendRetryStartMs >= RETRY_MS) {
        sendCloud();
        return;
    }
    if (readRetry && now - readRetryStartMs >= RETRY_MS) readMeter();
}

void onMeterEnabled() {
    readRetry = false;
    app.meterError[0] = 0;
}

uint32_t secondsToNextSend() {
    uint32_t elapsed = millis() - periodStartMs;
    uint32_t period = periodMs();
    return elapsed >= period ? 0 : (period - elapsed) / 1000;
}

}  // namespace poller
```

- [ ] **Шаг 2: `src/port/web.cpp` (заменить целиком)**

```cpp
#include "web.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include <memory>

#include "../app.h"
#include "../poller.h"
#include "log.h"
#include "net.h"
#include "wifi_portal.h"

namespace web {
namespace {

AsyncWebServer server(80);

const long BAUDS[] = {300, 600, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};

void sendJson(AsyncWebServerRequest* request, JsonDocument& doc) {
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void sendOk(AsyncWebServerRequest* request) { request->send(200, "application/json", "{\"ok\":true}"); }

void getStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["fw"] = FIRMWARE_VERSION;
    doc["ip"] = net::ip();
    doc["rssi"] = net::rssi();
    doc["uptime_s"] = millis() / 1000;
    doc["heap"] = ESP.getFreeHeap();
    doc["wifi_mode"] = net::modeName();

    doc["meter_enabled"] = app.sett.meterEnabled;
    doc["meter_reading"] = app.meterReading.load();
    doc["meter_error"] = app.meterError;

    doc["has_reading"] = app.hasReading;
    doc["read_at"] = app.lastReadAt;
    doc["serial"] = app.last.serial;
    doc["model"] = app.last.model;
    doc["meter_fw"] = app.last.fwVersion;
    doc["meter_time"] = app.last.time;
    doc["total"] = app.last.total;
    JsonArray tariffs = doc["tariffs"].to<JsonArray>();
    for (uint8_t i = 0; i < app.last.tariffCount && i < core::MAX_TARIFFS; ++i) tariffs.add(app.last.tariff[i]);

    doc["cloud_at"] = app.cloudAt;
    doc["cloud_code"] = app.cloudCode;
    doc["cloud_error"] = app.cloudError;
    doc["cloud_next_s"] = poller::secondsToNextSend();
    sendJson(request, doc);
}

void getSettings(AsyncWebServerRequest* request) {
    const core::Settings& s = app.sett;
    JsonDocument doc;
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
    doc["rfc_enabled"] = s.rfcEnabled;
    doc["rfc_port"] = s.rfcPort;
    sendJson(request, doc);
}

String param(AsyncWebServerRequest* request, const char* name) {
    String v = request->hasParam(name, true) ? request->getParam(name, true)->value() : String();
    v.trim();
    return v;
}

// Целое в диапазоне; иначе ошибка поля.
bool paramLong(AsyncWebServerRequest* request, const char* name, long lo, long hi, long& out,
               JsonObject errors, const char* message) {
    String v = param(request, name);
    char* end = nullptr;
    long x = strtol(v.c_str(), &end, 10);
    if (v.isEmpty() || *end || x < lo || x > hi) {
        errors[name] = message;
        return false;
    }
    out = x;
    return true;
}

// Строка, влезающая в буфер вместе с нулём.
bool paramStr(AsyncWebServerRequest* request, const char* name, char* dst, size_t cap,
              JsonObject errors, const char* message) {
    String v = param(request, name);
    if (v.length() >= cap) {
        errors[name] = message;
        return false;
    }
    snprintf(dst, cap, "%s", v.c_str());
    return true;
}

// Чекбокс: formSubmit шлёт 1 или 0, как в портале waterius.
bool paramBool(AsyncWebServerRequest* request, const char* name) { return param(request, name) == "1"; }

void postSettings(AsyncWebServerRequest* request) {
    core::Settings s = app.sett;
    JsonDocument doc;
    JsonObject errors = doc["errors"].to<JsonObject>();
    long v = 0;

    if (paramLong(request, "baud", 300, 115200, v, errors, "Выберите скорость из списка")) {
        bool known = false;
        for (long b : BAUDS) known = known || b == v;
        if (known) s.serial.baud = (uint32_t)v;
        else errors["baud"] = "Выберите скорость из списка";
    }
    if (paramLong(request, "bits", 5, 8, v, errors, "Биты данных: от 5 до 8")) s.serial.bits = (uint8_t)v;
    String parity = param(request, "parity");
    if (parity == "N" || parity == "E" || parity == "O") s.serial.parity = parity[0];
    else errors["parity"] = "Выберите чётность";
    if (paramLong(request, "stop", 1, 2, v, errors, "Стоп-биты: 1 или 2")) s.serial.stop = (uint8_t)v;

    s.meterEnabled = paramBool(request, "meter_enabled");
    if (paramLong(request, "meter_addr", 0, 127, v, errors, "Адрес: от 0 до 127")) s.meterAddr = (uint8_t)v;
    paramStr(request, "meter_pwd", s.meterPwd, sizeof(s.meterPwd), errors, "Пароль — до 16 символов");

    if (paramLong(request, "period_min", 1, 1440, v, errors, "Период: от 1 до 1440 минут")) s.periodMin = (uint16_t)v;
    if (paramStr(request, "host", s.host, sizeof(s.host), errors, "Адрес сервера — до 63 символов") &&
        strncmp(s.host, "http://", 7) != 0 && strncmp(s.host, "https://", 8) != 0)
        errors["host"] = "Адрес начинается с http:// или https://";
    paramStr(request, "key", s.key, sizeof(s.key), errors, "Ключ — до 40 символов");
    paramStr(request, "email", s.email, sizeof(s.email), errors, "E-mail — до 63 символов");

    s.rfcEnabled = paramBool(request, "rfc_enabled");
    if (paramLong(request, "rfc_port", 1, 65535, v, errors, "Порт: от 1 до 65535")) s.rfcPort = (uint16_t)v;

    if (errors.size()) {
        sendJson(request, doc);
        return;
    }

    bool reboot = s.rfcEnabled != app.sett.rfcEnabled || s.rfcPort != app.sett.rfcPort;
    app.pendingSettings = s;
    app.settingsPending.store(true);

    doc.remove("errors");
    doc["ok"] = true;
    doc["reboot"] = reboot;
    sendJson(request, doc);
}

// Текст лога после позиции from; страница log.html опрашивает раз в секунду.
void getLog(AsyncWebServerRequest* request) {
    uint32_t from = request->hasParam("from") ? strtoul(request->getParam("from")->value().c_str(), nullptr, 10) : 0;
    std::unique_ptr<char[]> text(new char[LogSink::SIZE + 1]);
    size_t len = 0;
    bool skipped = false;
    uint32_t next = Log.read(from, text.get(), LogSink::SIZE, len, skipped);
    text[len] = 0;

    JsonDocument doc;
    doc["boot"] = Log.bootId();
    doc["next"] = next;
    doc["skipped"] = skipped;
    doc["text"] = (const char*)text.get();  // const char* — без копии в документ
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

}  // namespace

void begin() {
    if (!LittleFS.begin()) Log.println("LittleFS не смонтирован: залейте образ командой uploadfs");

    server.on("/api/status", HTTP_GET, getStatus);
    server.on("/api/settings", HTTP_GET, getSettings);
    server.on("/api/settings", HTTP_POST, postSettings);
    server.on("/api/read", HTTP_POST, [](AsyncWebServerRequest* request) {
        app.readNow.store(true);
        sendOk(request);
    });
    server.on("/api/send", HTTP_POST, [](AsyncWebServerRequest* request) {
        app.sendNow.store(true);
        sendOk(request);
    });
    server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* request) {
        app.rebootNow.store(true);
        sendOk(request);
    });

    server.on("/api/log", HTTP_GET, getLog);
    wifi_portal::registerRoutes(server);

    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("no-cache");
    server.onNotFound([](AsyncWebServerRequest* request) {
        // Клиент точки доступа открыл чужой адрес — ведём в настройку Wi-Fi
        if (ON_AP_FILTER(request)) request->redirect("http://192.168.4.1/wifi.html");
        else request->send(404, "text/plain", "Not found");
    });
    server.begin();
}

void loop() {}

}  // namespace web
```

- [ ] **Шаг 3: `data/app.js` — вставить разделы статуса и настроек**

Вставить перед строкой `/* ---------- Wi-Fi ---------- */`:

```js
/* ---------- Статус ---------- */

function statusPage() {
    loadStatus();
    setInterval(loadStatus, 3000);
}

function loadStatus() {
    ajax('/api/status', {}, s => {
        let state = 'ок';
        if (s.meter_reading) state = 'идёт чтение…';
        else if (!s.meter_enabled) state = 'опрос выключен' + (s.meter_error ? ': ' + s.meter_error : '');
        else if (s.transparent) state = 'порт занят прозрачной сессией';
        else if (s.meter_error) state = s.meter_error;
        setText('meter-state', state);

        setText('serial', s.serial || '—');
        setText('model', s.model || '—');
        setText('meter-fw', s.meter_fw || '—');
        setText('meter-time', s.meter_time || '—');
        setText('read-at', s.has_reading ? fmtTime(s.read_at) : 'чтений не было');
        setText('total', s.has_reading ? fmtKwh(s.total) : '—');
        const rows = $('tariffs');
        rows.innerHTML = '';
        s.tariffs.forEach((v, i) => {
            const tr = rows.insertRow();
            tr.insertCell().textContent = 'T' + (i + 1) + ', кВт·ч';
            tr.insertCell().textContent = fmtKwh(v);
        });

        let cloud = 'не отправляли';
        if (s.cloud_error) cloud = s.cloud_error + (s.cloud_code ? ' (HTTP ' + s.cloud_code + ')' : '');
        else if (s.cloud_code) cloud = 'HTTP ' + s.cloud_code + ', ' + fmtTime(s.cloud_at);
        setText('cloud', cloud);
        setText('cloud-next', Math.ceil(s.cloud_next_s / 60) + ' мин');

        setText('fw', s.fw);
        setText('ip', s.ip || '—');
        setText('rssi', s.rssi ? s.rssi + ' дБм' : '—');
        setText('uptime', fmtUptime(s.uptime_s));
        setText('heap', Math.round(s.heap / 1024) + ' КБ');
        setText('wifi-mode', s.wifi_mode);
        $('btn-read').disabled = !s.meter_enabled;
    });
}

function action(url, btn) {
    btn.disabled = true;
    post(url, () => {
        btn.disabled = false;
        loadStatus();
    });
}

function reboot(btn) {
    if (!confirm('Перезагрузить устройство?')) return;
    btn.disabled = true;
    post('/api/reboot', () => {});
}

/* ---------- Настройки ---------- */

function settingsPage() {
    ajax('/api/settings', {}, s => {
        const form = $('settings');
        for (const k in s) {
            const inp = form.elements[k];
            if (!inp) continue;
            if (inp.type == 'checkbox') inp.checked = !!s[k];
            else inp.value = s[k];
        }
    });
}

function saveSettings(event, form) {
    $('saved').classList.add('hd');
    formSubmit(event, form, '/api/settings', res => {
        showOk('saved', res.reboot ? 'Сохранено, устройство перезагружается…' : 'Сохранено');
    });
}
```

- [ ] **Шаг 4: Создать `data/index.html` и `data/settings.html`**

```html
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>esp32-opto — статус</title>
    <link rel="stylesheet" href="/style.css">
    <script src="/app.js"></script>
</head>
<body onload="statusPage()">
<div class="wrap">
    <nav class="nav">
        <a class="on" href="/">Статус</a>
        <a href="/settings.html">Настройки</a>
        <a href="/wifi.html">Wi-Fi</a>
        <a href="/log.html">Лог</a>
        <a href="/update">Обновление</a>
    </nav>
    <p id="link-lost" class="form-error hd">Нет связи с устройством</p>

    <h2>Счётчик</h2>
    <table>
        <tr><td>Состояние</td><td id="meter-state">…</td></tr>
        <tr><td>Серийный номер</td><td id="serial">—</td></tr>
        <tr><td>Модель</td><td id="model">—</td></tr>
        <tr><td>Версия ПО</td><td id="meter-fw">—</td></tr>
        <tr><td>Время счётчика</td><td id="meter-time">—</td></tr>
    </table>

    <h2>Показания</h2>
    <table>
        <tr><td>Прочитаны</td><td id="read-at">—</td></tr>
        <tr><td>Всего, кВт·ч</td><td id="total">—</td></tr>
    </table>
    <table id="tariffs"></table>

    <h2>Облако</h2>
    <table>
        <tr><td>Последняя отправка</td><td id="cloud">—</td></tr>
        <tr><td>Следующая через</td><td id="cloud-next">—</td></tr>
    </table>
    <button class="btn" id="btn-read" onclick="action('/api/read', this)">Прочитать сейчас</button>
    <button class="btn" onclick="action('/api/send', this)">Отправить сейчас</button>

    <h2>Устройство</h2>
    <table>
        <tr><td>Прошивка</td><td id="fw">—</td></tr>
        <tr><td>IP</td><td id="ip">—</td></tr>
        <tr><td>Сигнал Wi-Fi</td><td id="rssi">—</td></tr>
        <tr><td>Режим Wi-Fi</td><td id="wifi-mode">—</td></tr>
        <tr><td>Работает</td><td id="uptime">—</td></tr>
        <tr><td>Свободная память</td><td id="heap">—</td></tr>
    </table>
    <button class="btn-serv" onclick="reboot(this)">Перезагрузить</button>
</div>
</body>
</html>
```

```html
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>esp32-opto — настройки</title>
    <link rel="stylesheet" href="/style.css">
    <script src="/app.js"></script>
</head>
<body onload="settingsPage()">
<div class="wrap">
    <nav class="nav">
        <a href="/">Статус</a>
        <a class="on" href="/settings.html">Настройки</a>
        <a href="/wifi.html">Wi-Fi</a>
        <a href="/log.html">Лог</a>
        <a href="/update">Обновление</a>
    </nav>
    <p id="link-lost" class="form-error hd">Нет связи с устройством</p>

    <form id="settings" onsubmit="saveSettings(event, this)">
        <h2>Счётчик НАРТИС</h2>
        <div class="chk">
            <input type="checkbox" id="meter_enabled" name="meter_enabled">
            <label for="meter_enabled">В работе — опрашивать счётчик</label>
        </div>
        <label for="meter_addr">Адрес (0 — определить самому)</label>
        <input id="meter_addr" name="meter_addr" type="number" min="0" max="127">
        <p class="error hd" id="meter_addr-error"></p>
        <label for="meter_pwd">Пароль LLS</label>
        <div class="pw">
            <input id="meter_pwd" name="meter_pwd" type="password" maxlength="16" autocomplete="off">
            <button type="button" onclick="showPW('meter_pwd')">показать</button>
        </div>
        <p class="error hd" id="meter_pwd-error"></p>
        <p class="text">Неверный пароль выключает опрос: после 5 ошибок счётчик блокируется на сутки.</p>

        <h2>Оптопорт</h2>
        <label for="baud">Скорость</label>
        <select id="baud" name="baud">
            <option>300</option><option>600</option><option>1200</option><option>2400</option>
            <option>4800</option><option>9600</option><option>19200</option><option>38400</option>
            <option>57600</option><option>115200</option>
        </select>
        <p class="error hd" id="baud-error"></p>
        <label for="bits">Биты данных</label>
        <select id="bits" name="bits"><option>5</option><option>6</option><option>7</option><option>8</option></select>
        <p class="error hd" id="bits-error"></p>
        <label for="parity">Чётность</label>
        <select id="parity" name="parity">
            <option value="N">нет</option><option value="E">чётность</option><option value="O">нечётность</option>
        </select>
        <p class="error hd" id="parity-error"></p>
        <label for="stop">Стоп-биты</label>
        <select id="stop" name="stop"><option>1</option><option>2</option></select>
        <p class="error hd" id="stop-error"></p>

        <h2>Облако Waterius</h2>
        <label for="period_min">Период выхода на связь, мин</label>
        <input id="period_min" name="period_min" type="number" min="1" max="1440">
        <p class="error hd" id="period_min-error"></p>
        <label for="host">Сервер</label>
        <input id="host" name="host" maxlength="63">
        <p class="error hd" id="host-error"></p>
        <label for="key">Ключ</label>
        <input id="key" name="key" maxlength="40">
        <p class="error hd" id="key-error"></p>
        <label for="email">E-mail</label>
        <input id="email" name="email" maxlength="63">
        <p class="error hd" id="email-error"></p>

        <h2>Прозрачный serial (RFC 2217)</h2>
        <div class="chk">
            <input type="checkbox" id="rfc_enabled" name="rfc_enabled">
            <label for="rfc_enabled">Включён</label>
        </div>
        <label for="rfc_port">TCP-порт</label>
        <input id="rfc_port" name="rfc_port" type="number" min="1" max="65535">
        <p class="error hd" id="rfc_port-error"></p>
        <p class="text">Смена этих двух полей перезагружает устройство.</p>

        <button class="btn" type="submit">Сохранить</button>
        <p class="ok hd" id="saved"></p>
    </form>
</div>
</body>
</html>
```

- [ ] **Шаг 5: Создать `tools/fake_cloud.py`**

```python
#!/usr/bin/env python3
"""Заглушка облака Waterius — проверить отправку и OTA без бэкенда.

    python3 tools/fake_cloud.py [--port 8080] [--ota firmware.bin] [--ota-fs littlefs.bin]

На странице /settings в поле «Сервер» указать http://<IP компьютера>:8080.

POST /api/source/iz/ печатает тело запроса. С --ota / --ota-fs первый ответ
содержит блок "ota" (формат сервера Waterius), файлы раздаются по
GET /firmware/<имя>. Бинарники после сборки лежат в
.pio/build/<env>/firmware.bin и .pio/build/<env>/littlefs.bin.
"""
import argparse
import hashlib
import http.server
import json
import os


def md5(path):
    with open(path, 'rb') as f:
        return hashlib.md5(f.read()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=8080)
    parser.add_argument('--ota', help='firmware.bin для блока ota.firmware')
    parser.add_argument('--ota-fs', help='littlefs.bin для блока ota.filesystem')
    args = parser.parse_args()

    files = {os.path.basename(p): p for p in (args.ota, args.ota_fs) if p}
    state = {'ota_sent': False}

    def image(host, path):
        return {'url': f'http://{host}/firmware/{os.path.basename(path)}',
                'md5': md5(path), 'size': os.path.getsize(path)}

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_POST(self):
            body = self.rfile.read(int(self.headers.get('Content-Length', 0)))
            print(self.path, 'Waterius-Token:', self.headers.get('Waterius-Token'))
            try:
                print(json.dumps(json.loads(body), ensure_ascii=False, indent=2))
            except ValueError:
                print(body)

            reply = b''
            if files and not state['ota_sent']:
                host = self.headers.get('Host')
                ota = {}
                if args.ota:
                    ota['firmware'] = image(host, args.ota)
                if args.ota_fs:
                    ota['filesystem'] = image(host, args.ota_fs)
                reply = json.dumps({'ota': ota}).encode()
                state['ota_sent'] = True
                print('-> отправлен блок ota')

            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(reply)))
            self.end_headers()
            self.wfile.write(reply)

        def do_GET(self):
            name = self.path.rsplit('/', 1)[-1]
            if not self.path.startswith('/firmware/') or name not in files:
                self.send_error(404)
                return
            with open(files[name], 'rb') as f:
                data = f.read()
            print('GET', self.path, len(data), 'байт')
            self.send_response(200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)

    print(f'Заглушка облака на порту {args.port}')
    http.server.ThreadingHTTPServer(('0.0.0.0', args.port), Handler).serve_forever()


if __name__ == '__main__':
    main()
```

- [ ] **Шаг 6: `src/main.cpp` (заменить целиком)**

Временная проверка чтения из задачи 2 уходит: её место занимает `poller`.

```cpp
// esp32-opto: счётчик НАРТИС через оптопорт → облако Waterius, веб-морда,
// прозрачный serial по RFC 2217.
// Дизайн: docs/superpowers/specs/2026-09-17-esp32-opto-firmware-design.md
#include <Arduino.h>

#include "app.h"
#include "poller.h"
#include "port/log.h"
#include "port/net.h"
#include "port/opto_bus.h"
#include "port/storage.h"
#include "port/web.h"
#include "port/wifi_portal.h"

AppState app;

namespace {

// Настройки со страницы /settings. В NVS пишет только loop().
void applyPendingSettings() {
    if (!app.settingsPending.exchange(false)) return;
    core::Settings next = app.pendingSettings;
    // Сеть меняется только со страницы /wifi — не затираем её копией из формы
    memcpy(next.ssid, app.sett.ssid, sizeof(next.ssid));
    memcpy(next.pass, app.sett.pass, sizeof(next.pass));
    memcpy(next.bssid, app.sett.bssid, sizeof(next.bssid));
    next.channel = app.sett.channel;

    bool meterTurnedOn = next.meterEnabled && !app.sett.meterEnabled;
    bool reboot = next.rfcEnabled != app.sett.rfcEnabled || next.rfcPort != app.sett.rfcPort;
    app.sett = next;
    storage::saveSettings(app.sett);
    Log.println("Настройки сохранены");
    if (meterTurnedOn) poller::onMeterEnabled();
    if (reboot) app.rebootNow.store(true);  // сервер RFC 2217 на ходу не перезапускается
}

// Канал и BSSID роутера после подключения — для быстрого коннекта (как в waterius).
void saveFastConnect() {
    uint8_t channel = 0;
    uint8_t bssid[6];
    if (!net::takeFastConnect(channel, bssid)) return;
    if (channel == app.sett.channel && memcmp(bssid, app.sett.bssid, sizeof(bssid)) == 0) return;
    app.sett.channel = channel;
    memcpy(app.sett.bssid, bssid, sizeof(bssid));
    storage::saveSettings(app.sett);
}

}  // namespace

void setup() {
    Log.begin(115200);
    delay(200);
    Log.printf("esp32-opto %s\n", FIRMWARE_VERSION);

    storage::loadSettings(app.sett);
    app.hasReading = storage::loadLastReading(app.last, app.lastReadAt);
    app.otaError = storage::loadOtaError();

    bus.begin(app.sett.serial);
    net::begin(app.sett);
    web::begin();
    poller::begin();
}

void loop() {
    net::loop(app.sett);
    saveFastConnect();
    wifi_portal::loop();
    web::loop();
    applyPendingSettings();
    poller::loop();
    if (app.rebootNow.load()) {
        delay(300);
        ESP.restart();
    }
    delay(2);
}
```

- [ ] **Шаг 7: Сборка**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3 -e esp32-c3`
Expected: код возврата 0, оба env `SUCCESS`.

Run: `~/.platformio/penv/bin/pio run -e esp32-c3 -t buildfs`
Expected: код возврата 0.

- [ ] **Шаг 8: На железе (владелец)**

1. `uploadfs`, затем `upload`. Открыть `http://<IP>/` → через 30 секунд после старта в «Показаниях» появляются значения, в логе `Счётчик: всего … кВт·ч, тарифов N, sn …`.
2. На компьютере: `python3 tools/fake_cloud.py`. На `/settings`: «Сервер» — `http://<IP компьютера>:8080`, «Ключ» — любая строка, «Сохранить» → «Сохранено». На `/` — «Отправить сейчас» → заглушка печатает JSON с `key`, `sn`, `total`, `total1…`, `data_type…`, `meter_read_at`, `"ota_error": 0`; в статусе «HTTP 200, <время>».
3. Остановить заглушку → «Отправить сейчас» → в статусе «нет соединения (HTTP -1)» или другой отрицательный код; через 5 минут в логе новая попытка.
4. **Неверный пароль (одна попытка из пяти допустимых):** на `/settings` поставить пароль `000`, сохранить, «Прочитать сейчас» → состояние «опрос выключен: счётчик отверг пароль (адрес 16)», тумблер «В работе» снят, кнопка «Прочитать сейчас» неактивна. Перезагрузить — состояние сохранилось. Вернуть пароль `111`, включить тумблер, сохранить → «Прочитать сейчас» снова читает.
5. Снять тумблер «В работе», запустить заглушку, «Отправить сейчас» → в JSON то же `meter_read_at`, что и в прошлой отправке.

- [ ] **Шаг 9: Коммит**

```bash
git add src/poller.h src/poller.cpp src/port/web.cpp data tools src/main.cpp
git commit -m "feat: опрос счётчика, отправка в облако, страницы статуса и настроек"
```

---

## Задача 5: Прозрачный serial по RFC 2217

**Files:**
- Create: `lib/rfc2217-server/` (копия + `library.json` + `PATCHES.md` + патч), `src/port/rfc2217.h`, `src/port/rfc2217.cpp`
- Modify: `src/port/web.cpp` (2 правки), `src/main.cpp` (заменить целиком)

**Interfaces:**
- Consumes: `bus.requestPreempt/owner/beginTransparent/endTransparent/abortRequested/configure/current/read/write/available` (задача 1); `NartisMeter` прерывается через `abortRequested()` (задача 2); `app.sett.rfcEnabled/rfcPort` (задача 1).
- Produces: `rfc2217::begin(uint16_t port)`, `rfc2217::loop()`, `rfc2217::active() -> bool`; в `GET /api/status` поле `transparent`.

Как это работает:
- Сервер igrr живёт в своих pthread-задачах. Колбэк подключения ставит `bus.requestPreempt()` — идущий `NartisMeter::read` выходит с `Aborted` в течение миллисекунды.
- Байты от клиента колбэк кладёт в `StreamBuffer`; `loop()` пишет их в UART и отдаёт клиенту ответ счётчика через `rfc2217_server_send_data`.
- Смену скорости, битов, чётности и стоп-битов колбэки запоминают в атомарных полях; `loop()` применяет их до данных, пришедших после команды.
- Отключение клиента → `bus.endTransparent()`: шина свободна, запрос прерывания снят.

- [ ] **Шаг 1: Скопировать библиотеку на зафиксированном коммите**

```bash
tmp=$(mktemp -d)
git clone https://github.com/igrr/rfc2217-server.git "$tmp/rfc2217-server"
git -C "$tmp/rfc2217-server" checkout 281e424d4bca6c85fb8e4d727dd1fa0c315b95f1
mkdir -p lib/rfc2217-server
cp -r "$tmp/rfc2217-server/include" "$tmp/rfc2217-server/src" "$tmp/rfc2217-server/LICENSE.txt" lib/rfc2217-server/
rm -rf "$tmp"
```

- [ ] **Шаг 2: Создать `lib/rfc2217-server/library.json` и `lib/rfc2217-server/PATCHES.md`**

```json
{
  "name": "rfc2217-server",
  "version": "0.4.0",
  "license": "Apache-2.0",
  "frameworks": "*",
  "platforms": "espressif32",
  "build": { "srcDir": "src", "includeDir": "include" }
}
```

```markdown
# rfc2217-server — копия с патчем esp32-opto

Исходник: https://github.com/igrr/rfc2217-server, коммит
`281e424d4bca6c85fb8e4d727dd1fa0c315b95f1`, лицензия Apache-2.0
(`LICENSE.txt`). Скопированы `include/`, `src/`, `LICENSE.txt`;
`library.json` добавлен для PlatformIO.

Изменения (помечены в коде `esp32-opto patch`):

1. `on_client_connected` вызывается сразу после `accept()`, а не после
   согласования опции COM-PORT. Иначе простой TCP-клиент не прерывал бы
   опрос счётчика.
2. Добавлены колбэки `on_datasize`, `on_parity`, `on_stopsize`
   (`rfc2217_on_line_param_t`). Исходная библиотека только подтверждала эти
   команды клиенту и ничего не меняла.
```

- [ ] **Шаг 3: Патч `lib/rfc2217-server/include/rfc2217_server.h`**

Правка 1 в `lib/rfc2217-server/include/rfc2217_server.h` — найти:

```
typedef void (*rfc2217_on_data_received_t)(void *ctx, const uint8_t *data, size_t len);
```

заменить на:

```
typedef void (*rfc2217_on_data_received_t)(void *ctx, const uint8_t *data, size_t len);

/**
 * @brief data size / parity / stop size change request callback (esp32-opto patch)
 *
 * @param ctx context pointer passed to rfc2217_server_create
 * @param requested value from the RFC 2217 command, 0 means "report current"
 * @return value reported back to the client
 */
typedef unsigned (*rfc2217_on_line_param_t)(void *ctx, unsigned requested);
```

Правка 2 в `lib/rfc2217-server/include/rfc2217_server.h` — найти:

```
    rfc2217_on_data_received_t on_data_received;    //!< callback called when data is received from client
```

заменить на:

```
    rfc2217_on_data_received_t on_data_received;    //!< callback called when data is received from client
    rfc2217_on_line_param_t on_datasize;    //!< SET-DATASIZE request (esp32-opto patch)
    rfc2217_on_line_param_t on_parity;      //!< SET-PARITY request (esp32-opto patch)
    rfc2217_on_line_param_t on_stopsize;    //!< SET-STOPSIZE request (esp32-opto patch)
```

- [ ] **Шаг 4: Патч `lib/rfc2217-server/src/rfc2217_server.c`**

Правка 1 в `lib/rfc2217-server/src/rfc2217_server.c` — найти:

```
        server->tcp_receive_thread_shutdown = false;
        pthread_create(&server->tcp_receive_thread, NULL, tcp_receive_thread_fn, server);
```

заменить на:

```
        // esp32-opto patch: report the client right after accept(), without waiting
        // for RFC 2217 negotiation - a plain TCP client must stop meter polling too
        if (server->config.on_client_connected) {
            server->config.on_client_connected(server->config.ctx);
        }

        server->tcp_receive_thread_shutdown = false;
        pthread_create(&server->tcp_receive_thread, NULL, tcp_receive_thread_fn, server);
```

Правка 2 в `lib/rfc2217-server/src/rfc2217_server.c` — найти:

```
        server->client_is_rfc2217 = true;
        if (server->config.on_client_connected) {
            server->config.on_client_connected(server->config.ctx);
        }
```

заменить на:

```
        server->client_is_rfc2217 = true;
        // esp32-opto patch: on_client_connected is called on accept() in server_thread_fn
```

Правка 3 в `lib/rfc2217-server/src/rfc2217_server.c` — найти:

```
    } else if (subnegotiation == T_SET_DATASIZE) {
        uint8_t datasize = server->suboption[2];
        ESP_LOGD(TAG, "Set datasize: %d - not supported, accepting", datasize);
        rfc2217_send_subnegotiation(server, T_SERVER_SET_DATASIZE, &server->suboption[2], 1);
    } else if (subnegotiation == T_SET_PARITY) {
        uint8_t parity = server->suboption[2];
        ESP_LOGD(TAG, "Set parity: %d - not supported, accepting", parity);
        rfc2217_send_subnegotiation(server, T_SERVER_SET_PARITY, &server->suboption[2], 1);
    } else if (subnegotiation == T_SET_STOPSIZE) {
        uint8_t stopsize = server->suboption[2];
        ESP_LOGD(TAG, "Set stopsize: %d - not supported, accepting", stopsize);
        rfc2217_send_subnegotiation(server, T_SERVER_SET_STOPSIZE, &server->suboption[2], 1);
```

заменить на:

```
    } else if (subnegotiation == T_SET_DATASIZE) {
        // esp32-opto patch: data size, parity and stop size are applied via callbacks
        uint8_t data[1] = {server->suboption[2]};
        if (server->config.on_datasize) {
            data[0] = (uint8_t)server->config.on_datasize(server->config.ctx, data[0]);
        }
        ESP_LOGD(TAG, "Set datasize: requested %d, accepted %d", server->suboption[2], data[0]);
        rfc2217_send_subnegotiation(server, T_SERVER_SET_DATASIZE, data, 1);
    } else if (subnegotiation == T_SET_PARITY) {
        uint8_t data[1] = {server->suboption[2]};
        if (server->config.on_parity) {
            data[0] = (uint8_t)server->config.on_parity(server->config.ctx, data[0]);
        }
        ESP_LOGD(TAG, "Set parity: requested %d, accepted %d", server->suboption[2], data[0]);
        rfc2217_send_subnegotiation(server, T_SERVER_SET_PARITY, data, 1);
    } else if (subnegotiation == T_SET_STOPSIZE) {
        uint8_t data[1] = {server->suboption[2]};
        if (server->config.on_stopsize) {
            data[0] = (uint8_t)server->config.on_stopsize(server->config.ctx, data[0]);
        }
        ESP_LOGD(TAG, "Set stopsize: requested %d, accepted %d", server->suboption[2], data[0]);
        rfc2217_send_subnegotiation(server, T_SERVER_SET_STOPSIZE, data, 1);
```

- [ ] **Шаг 5: Создать `src/port/rfc2217.h` и `src/port/rfc2217.cpp`**

```cpp
// Порт: прозрачный serial по RFC 2217 поверх igrr/rfc2217-server (lib/rfc2217-server).
// Сервер работает в своих задачах; его колбэки только кладут байты в буфер и
// ставят флаги. С UART работает loop() через OptoBus.
#pragma once
#include <stdint.h>

namespace rfc2217 {
void begin(uint16_t port);
void loop();
bool active();  // клиент подключён и оптопорт у него
}  // namespace rfc2217
```

```cpp
#include "rfc2217.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>

#include <atomic>

#include "log.h"
#include "opto_bus.h"
#include "rfc2217_server.h"

namespace rfc2217 {
namespace {

const size_t FROM_CLIENT_BUFFER = 1024;

rfc2217_server_t server = nullptr;
StreamBufferHandle_t fromClient = nullptr;  // клиент → UART: пишет поток сервера, читает loop()
std::atomic<bool> connected{false};

// Запрошенные клиентом параметры порта; 0 — не менялись. Применяет loop().
std::atomic<uint32_t> wantBaud{0};
std::atomic<uint8_t> wantBits{0};
std::atomic<char> wantParity{0};
std::atomic<uint8_t> wantStop{0};

void onConnected(void*) {
    wantBaud.store(0);
    wantBits.store(0);
    wantParity.store(0);
    wantStop.store(0);
    bus.requestPreempt();  // идущий опрос счётчика прерывается сразу
    connected.store(true);
}

void onDisconnected(void*) { connected.store(false); }

void onData(void*, const uint8_t* data, size_t len) {
    xStreamBufferSend(fromClient, data, len, 0);  // не влезло — теряем, как переполненный UART
}

// Значение 0 во всех SET-командах RFC 2217 — «сообщите текущее».
unsigned onBaudrate(void*, unsigned requested) {
    if (requested) {
        wantBaud.store(requested);
        return requested;
    }
    uint32_t w = wantBaud.load();
    return w ? w : bus.current().baud;
}

unsigned onDatasize(void*, unsigned requested) {
    if (requested >= 5 && requested <= 8) {
        wantBits.store((uint8_t)requested);
        return requested;
    }
    uint8_t w = wantBits.load();
    return w ? w : bus.current().bits;
}

// RFC 2217: 1 — нет, 2 — нечётность, 3 — чётность. MARK и SPACE не поддерживаем.
unsigned onParity(void*, unsigned requested) {
    char p = requested == 1 ? 'N' : (requested == 2 ? 'O' : (requested == 3 ? 'E' : 0));
    if (p) {
        wantParity.store(p);
        return requested;
    }
    char c = wantParity.load();
    if (!c) c = bus.current().parity;
    return c == 'O' ? 2 : (c == 'E' ? 3 : 1);
}

// RFC 2217: 1 — один стоп-бит, 2 — два. 1,5 не поддерживаем.
unsigned onStopsize(void*, unsigned requested) {
    if (requested == 1 || requested == 2) {
        wantStop.store((uint8_t)requested);
        return requested;
    }
    uint8_t w = wantStop.load();
    return w ? w : bus.current().stop;
}

}  // namespace

void begin(uint16_t port) {
    fromClient = xStreamBufferCreate(FROM_CLIENT_BUFFER, 1);
    rfc2217_server_config_t cfg = {};
    cfg.on_client_connected = onConnected;
    cfg.on_client_disconnected = onDisconnected;
    cfg.on_baudrate = onBaudrate;
    cfg.on_data_received = onData;
    cfg.on_datasize = onDatasize;
    cfg.on_parity = onParity;
    cfg.on_stopsize = onStopsize;
    cfg.port = port;
    cfg.task_stack_size = 4096;
    cfg.task_priority = 5;
    cfg.task_core_id = 0;
    if (rfc2217_server_create(&cfg, &server) != 0 || rfc2217_server_start(server) != 0) {
        Log.println("RFC 2217: сервер не запустился");
        server = nullptr;
        return;
    }
    Log.printf("RFC 2217: порт %u\n", port);
}

void loop() {
    if (!server) return;

    if (connected.load()) {
        if (bus.owner() != BusOwner::Transparent) {
            bus.beginTransparent();
            Log.println("RFC 2217: клиент подключился, опрос счётчика остановлен");
        }
    } else if (bus.owner() == BusOwner::Transparent || bus.abortRequested()) {
        bus.endTransparent();
        xStreamBufferReset(fromClient);
        Log.println("RFC 2217: клиент отключился, порт свободен");
    }
    if (bus.owner() != BusOwner::Transparent) return;

    // Параметры порта от клиента — до данных, пришедших после них
    core::SerialCfg cfg = bus.current();
    bool changed = false;
    uint32_t baud = wantBaud.exchange(0);
    if (baud) { cfg.baud = baud; changed = true; }
    uint8_t bits = wantBits.exchange(0);
    if (bits) { cfg.bits = bits; changed = true; }
    char parity = wantParity.exchange(0);
    if (parity) { cfg.parity = parity; changed = true; }
    uint8_t stop = wantStop.exchange(0);
    if (stop) { cfg.stop = stop; changed = true; }
    if (changed) bus.configure(cfg);

    uint8_t buf[128];
    size_t n;
    while ((n = xStreamBufferReceive(fromClient, buf, sizeof(buf), 0)) > 0) bus.write(buf, n);

    n = 0;
    while (n < sizeof(buf) && bus.available() > 0) {
        int c = bus.read();
        if (c < 0) break;
        buf[n++] = (uint8_t)c;
    }
    if (n) rfc2217_server_send_data(server, buf, n);
}

bool active() { return connected.load() && bus.owner() == BusOwner::Transparent; }

}  // namespace rfc2217
```

- [ ] **Шаг 6: `src/port/web.cpp` — поле `transparent` в статусе**

Правка 1 в `src/port/web.cpp` — найти:

```
#include "net.h"
```

заменить на:

```
#include "net.h"
#include "rfc2217.h"
```

Правка 2 в `src/port/web.cpp` — найти:

```
    doc["meter_error"] = app.meterError;
```

заменить на:

```
    doc["meter_error"] = app.meterError;
    doc["transparent"] = rfc2217::active();
```

- [ ] **Шаг 7: `src/main.cpp` (заменить целиком)**

```cpp
// esp32-opto: счётчик НАРТИС через оптопорт → облако Waterius, веб-морда,
// прозрачный serial по RFC 2217.
// Дизайн: docs/superpowers/specs/2026-09-17-esp32-opto-firmware-design.md
#include <Arduino.h>

#include "app.h"
#include "poller.h"
#include "port/log.h"
#include "port/net.h"
#include "port/opto_bus.h"
#include "port/rfc2217.h"
#include "port/storage.h"
#include "port/web.h"
#include "port/wifi_portal.h"

AppState app;

namespace {

// Настройки со страницы /settings. В NVS пишет только loop().
void applyPendingSettings() {
    if (!app.settingsPending.exchange(false)) return;
    core::Settings next = app.pendingSettings;
    // Сеть меняется только со страницы /wifi — не затираем её копией из формы
    memcpy(next.ssid, app.sett.ssid, sizeof(next.ssid));
    memcpy(next.pass, app.sett.pass, sizeof(next.pass));
    memcpy(next.bssid, app.sett.bssid, sizeof(next.bssid));
    next.channel = app.sett.channel;

    bool meterTurnedOn = next.meterEnabled && !app.sett.meterEnabled;
    bool reboot = next.rfcEnabled != app.sett.rfcEnabled || next.rfcPort != app.sett.rfcPort;
    app.sett = next;
    storage::saveSettings(app.sett);
    Log.println("Настройки сохранены");
    if (meterTurnedOn) poller::onMeterEnabled();
    if (reboot) app.rebootNow.store(true);  // сервер RFC 2217 на ходу не перезапускается
}

// Канал и BSSID роутера после подключения — для быстрого коннекта (как в waterius).
void saveFastConnect() {
    uint8_t channel = 0;
    uint8_t bssid[6];
    if (!net::takeFastConnect(channel, bssid)) return;
    if (channel == app.sett.channel && memcmp(bssid, app.sett.bssid, sizeof(bssid)) == 0) return;
    app.sett.channel = channel;
    memcpy(app.sett.bssid, bssid, sizeof(bssid));
    storage::saveSettings(app.sett);
}

}  // namespace

void setup() {
    Log.begin(115200);
    delay(200);
    Log.printf("esp32-opto %s\n", FIRMWARE_VERSION);

    storage::loadSettings(app.sett);
    app.hasReading = storage::loadLastReading(app.last, app.lastReadAt);
    app.otaError = storage::loadOtaError();

    bus.begin(app.sett.serial);
    net::begin(app.sett);
    web::begin();
    if (app.sett.rfcEnabled) rfc2217::begin(app.sett.rfcPort);
    poller::begin();
}

void loop() {
    net::loop(app.sett);
    saveFastConnect();
    wifi_portal::loop();
    web::loop();
    rfc2217::loop();
    applyPendingSettings();
    poller::loop();
    if (app.rebootNow.load()) {
        delay(300);
        ESP.restart();
    }
    delay(2);
}
```

- [ ] **Шаг 8: Сборка**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3 -e esp32-c3`
Expected: код возврата 0, оба env `SUCCESS`.

- [ ] **Шаг 9: На железе (владелец)**

На компьютере: `pip install pyserial`. Скрипт отправляет через устройство кадр SNRM на адрес 16 и ждёт UA от счётчика (кадры посчитаны с HCS; для адреса 17 — `7e a0 08 02 23 41 93 e8 01 7e` и `7e a0 08 02 23 41 53 e4 c7 7e`):

```bash
python3 - <<'PY'
import serial, time
s = serial.serial_for_url('rfc2217://192.168.x.y:2217', baudrate=9600, bytesize=8, parity='N', stopbits=1, timeout=2)
s.write(bytes.fromhex('7e a0 08 02 21 41 93 50 b4 7e'))   # SNRM, адрес 16
print('ответ:', s.read(64).hex(' '))                       # ожидается кадр 7e a0 … 73 … 7e (UA)
s.write(bytes.fromhex('7e a0 08 02 21 41 53 5c 72 7e'))   # DISC
time.sleep(1)
s.close()
PY
```

Expected:
1. В логе устройства `RFC 2217: клиент подключился, опрос счётчика остановлен`; скрипт печатает кадр, начинающийся с `7e a0` и содержащий `73`; после закрытия — `RFC 2217: клиент отключился, порт свободен`.
2. Пока скрипт держит соединение (добавить `time.sleep(30)` перед `close`): на `/` состояние «порт занят прозрачной сессией»; «Отправить сейчас» уходит в заглушку облака с прежним `meter_read_at`.
3. Нажать «Прочитать сейчас» и в течение секунды запустить скрипт → в логе `Счётчик: чтение прервано прозрачной сессией`, скрипт получает UA.
4. Windows (по желанию): HW VSP3 → виртуальный COM на `192.168.x.y:2217` → Nartis Tools на этом COM читает счётчик.

- [ ] **Шаг 10: Коммит**

```bash
git add lib/rfc2217-server src/port/rfc2217.h src/port/rfc2217.cpp src/port/web.cpp src/main.cpp
git commit -m "feat: прозрачный serial по RFC 2217 на igrr/rfc2217-server с патчем"
```

---

## Задача 6: OTA — страница /update, ArduinoOTA, сервер Waterius

**Files:**
- Create: `src/port/ota_cloud.h`, `src/port/ota_cloud.cpp`
- Modify: `src/poller.cpp` (3 правки), `src/port/web.cpp` (3 правки), `src/main.cpp` (заменить целиком)

**Interfaces:**
- Consumes: `core::parseOta`, `core::OtaRequest`, `core::OtaError`, `storage::saveOtaError`, `app.otaError` (задача 1); `net::tlsClient()` (задача 3); `rfc2217::active()` (задача 5).
- Produces: `ota_cloud::run(const core::OtaRequest&) -> uint8_t` (возвращается только при ошибке); страница `/update`; ArduinoOTA на порту 3232.

Порядок как в waterius `ESP8266/src/ota_update.cpp`: сначала образ ФС, потом прошивка, затем перезагрузка; код ошибки уходит в следующем запросе полем `ota_error` и обнуляется после успешной отправки. Загрузчик свой (`HTTPClient` + `Update.setMD5`): `HTTPUpdate` в arduino-esp32 2.0.17 не умеет `setMD5sum`.

- [ ] **Шаг 1: Создать `src/port/ota_cloud.h` и `src/port/ota_cloud.cpp`**

```cpp
// Порт: OTA через сервер Waterius — как ESP8266/src/ota_update.cpp в waterius,
// но свой загрузчик: HTTPUpdate в arduino-esp32 2.0.17 не умеет setMD5sum.
#pragma once
#include "../core/cloud.h"

namespace ota_cloud {

// Сначала образ ФС, потом прошивка, затем перезагрузка.
// Возвращается только при ошибке — с кодом core::OtaError для поля ota_error.
uint8_t run(const core::OtaRequest& req);

}  // namespace ota_cloud
```

```cpp
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
    Update.setMD5(img.md5);
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
```

- [ ] **Шаг 2: `src/poller.cpp` — разбор блока `ota` после успешной отправки**

Правка 1 в `src/poller.cpp` — найти:

```
#include "port/opto_bus.h"
#include "port/storage.h"
```

заменить на:

```
#include "port/opto_bus.h"
#include "port/ota_cloud.h"
#include "port/rfc2217.h"
#include "port/storage.h"
```

Правка 2 в `src/poller.cpp` — найти:

```
void sendCloud() {
```

заменить на:

```
// Блок ota в ответе облака — как в waterius main.cpp после send_data.
void handleOta(const String& response) {
    core::OtaRequest req;
    core::OtaParse parsed = core::parseOta(response.c_str(), req);
    if (parsed == core::OtaParse::None) return;
    if (parsed == core::OtaParse::Error) {
        app.otaError = core::OTA_ERR_PARSE;
        storage::saveOtaError(app.otaError);
        return;
    }
    if (rfc2217::active()) {
        Log.println("OTA: пропущено, идёт прозрачная сессия — сервер пришлёт блок снова");
        return;
    }
    app.otaError = ota_cloud::run(req);  // при успехе перезагрузка и сюда не вернёмся
    storage::saveOtaError(app.otaError);
}

void sendCloud() {
```

Правка 3 в `src/poller.cpp` — найти:

```
        storage::saveOtaError(0);
    }
}
```

заменить на:

```
        storage::saveOtaError(0);
    }
    handleOta(response);
}
```

- [ ] **Шаг 3: `src/port/web.cpp` — ElegantOTA**

Правка 1 в `src/port/web.cpp` — найти:

```
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
```

заменить на:

```
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <LittleFS.h>
```

Правка 2 в `src/port/web.cpp` — найти:

```
    wifi_portal::registerRoutes(server);
```

заменить на:

```
    wifi_portal::registerRoutes(server);
    ElegantOTA.begin(&server);  // страница /update: прошивка и образ LittleFS
```

Правка 3 в `src/port/web.cpp` — найти:

```
void loop() {}
```

заменить на:

```
void loop() { ElegantOTA.loop(); }
```

- [ ] **Шаг 4: `src/main.cpp` (заменить целиком)**

ArduinoOTA стартует, когда появилась сеть; mDNS выключен (спека, раздел 7).

```cpp
// esp32-opto: счётчик НАРТИС через оптопорт → облако Waterius, веб-морда,
// прозрачный serial по RFC 2217.
// Дизайн: docs/superpowers/specs/2026-09-17-esp32-opto-firmware-design.md
#include <Arduino.h>
#include <ArduinoOTA.h>

#include "app.h"
#include "poller.h"
#include "port/log.h"
#include "port/net.h"
#include "port/opto_bus.h"
#include "port/rfc2217.h"
#include "port/storage.h"
#include "port/web.h"
#include "port/wifi_portal.h"

AppState app;

namespace {

// Настройки со страницы /settings. В NVS пишет только loop().
void applyPendingSettings() {
    if (!app.settingsPending.exchange(false)) return;
    core::Settings next = app.pendingSettings;
    // Сеть меняется только со страницы /wifi — не затираем её копией из формы
    memcpy(next.ssid, app.sett.ssid, sizeof(next.ssid));
    memcpy(next.pass, app.sett.pass, sizeof(next.pass));
    memcpy(next.bssid, app.sett.bssid, sizeof(next.bssid));
    next.channel = app.sett.channel;

    bool meterTurnedOn = next.meterEnabled && !app.sett.meterEnabled;
    bool reboot = next.rfcEnabled != app.sett.rfcEnabled || next.rfcPort != app.sett.rfcPort;
    app.sett = next;
    storage::saveSettings(app.sett);
    Log.println("Настройки сохранены");
    if (meterTurnedOn) poller::onMeterEnabled();
    if (reboot) app.rebootNow.store(true);  // сервер RFC 2217 на ходу не перезапускается
}

// Канал и BSSID роутера после подключения — для быстрого коннекта (как в waterius).
void saveFastConnect() {
    uint8_t channel = 0;
    uint8_t bssid[6];
    if (!net::takeFastConnect(channel, bssid)) return;
    if (channel == app.sett.channel && memcmp(bssid, app.sett.bssid, sizeof(bssid)) == 0) return;
    app.sett.channel = channel;
    memcpy(app.sett.bssid, bssid, sizeof(bssid));
    storage::saveSettings(app.sett);
}

// ArduinoOTA — для pio run -t upload --upload-port <IP>. Стартует, когда появилась сеть.
void arduinoOta() {
    static bool started = false;
    if (!started) {
        if (!net::connected()) return;
        ArduinoOTA.setHostname(net::apName());
        ArduinoOTA.setMdnsEnabled(false);
        ArduinoOTA.begin();
        started = true;
    }
    ArduinoOTA.handle();
}

}  // namespace

void setup() {
    Log.begin(115200);
    delay(200);
    Log.printf("esp32-opto %s\n", FIRMWARE_VERSION);

    storage::loadSettings(app.sett);
    app.hasReading = storage::loadLastReading(app.last, app.lastReadAt);
    app.otaError = storage::loadOtaError();

    bus.begin(app.sett.serial);
    net::begin(app.sett);
    web::begin();
    if (app.sett.rfcEnabled) rfc2217::begin(app.sett.rfcPort);
    poller::begin();
}

void loop() {
    net::loop(app.sett);
    saveFastConnect();
    wifi_portal::loop();
    web::loop();
    arduinoOta();
    rfc2217::loop();
    applyPendingSettings();
    poller::loop();
    if (app.rebootNow.load()) {
        delay(300);
        ESP.restart();
    }
    delay(2);
}
```

- [ ] **Шаг 5: Сборка**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3 -e esp32-c3`
Expected: код возврата 0, оба env `SUCCESS`.

- [ ] **Шаг 6: На железе (владелец)**

1. `http://<IP>/update` → страница ElegantOTA. Загрузить `.pio/build/esp32-s3/firmware.bin` → устройство перезагружается. Режим «Filesystem», загрузить `.pio/build/esp32-s3/littlefs.bin` (после `-t buildfs`) → страницы открываются, настройки на `/settings` сохранились.
2. `~/.platformio/penv/bin/pio run -e esp32-s3 -t upload --upload-port 192.168.x.y` → прошивка по воздуху.
3. OTA через сервер: в `platformio.ini` поставить `version = "\"0.2.1\""`, собрать `pio run -e esp32-s3` и `pio run -e esp32-s3 -t buildfs`. Запустить `python3 tools/fake_cloud.py --ota .pio/build/esp32-s3/firmware.bin --ota-fs .pio/build/esp32-s3/littlefs.bin`, «Сервер» на `/settings` — заглушка. «Отправить сейчас» → заглушка печатает `-> отправлен блок ota`, два `GET /firmware/…`; устройство перезагружается, на `/` «Прошивка 0.2.1». Вернуть версию: `git checkout platformio.ini`.
4. Ошибка OTA: `python3 tools/fake_cloud.py --ota tools/fake_cloud.py` → «Отправить сейчас» → в логе `OTA: …` с ошибкой, перезагрузки нет. Ещё раз «Отправить сейчас» → в JSON заглушки `"ota_error": 3`; следующая отправка — `"ota_error": 0`.

- [ ] **Шаг 7: Коммит**

```bash
git add src/port/ota_cloud.h src/port/ota_cloud.cpp src/poller.cpp src/port/web.cpp src/main.cpp
git commit -m "feat: OTA через /update, ArduinoOTA и сервер Waterius"
```

---

## Задача 7: Сброс кнопкой BOOT и документация

**Files:**
- Modify: `src/main.cpp` (заменить целиком), `CLAUDE.md` (9 правок), `README.md` (заменить целиком)

**Interfaces:**
- Consumes: `storage::resetAll()` (задача 1), флаг `BOOT_PIN` из `platformio.ini` (задача 1).
- Produces: удержание BOOT 5 секунд — сброс к заводским настройкам.

- [ ] **Шаг 1: `src/main.cpp` (заменить целиком)**

```cpp
// esp32-opto: счётчик НАРТИС через оптопорт → облако Waterius, веб-морда,
// прозрачный serial по RFC 2217.
// Дизайн: docs/superpowers/specs/2026-09-17-esp32-opto-firmware-design.md
#include <Arduino.h>
#include <ArduinoOTA.h>

#include "app.h"
#include "poller.h"
#include "port/log.h"
#include "port/net.h"
#include "port/opto_bus.h"
#include "port/rfc2217.h"
#include "port/storage.h"
#include "port/web.h"
#include "port/wifi_portal.h"

#ifndef BOOT_PIN
#define BOOT_PIN 0
#endif

AppState app;

namespace {

const uint32_t FACTORY_RESET_HOLD_MS = 5000;

// Настройки со страницы /settings. В NVS пишет только loop().
void applyPendingSettings() {
    if (!app.settingsPending.exchange(false)) return;
    core::Settings next = app.pendingSettings;
    // Сеть меняется только со страницы /wifi — не затираем её копией из формы
    memcpy(next.ssid, app.sett.ssid, sizeof(next.ssid));
    memcpy(next.pass, app.sett.pass, sizeof(next.pass));
    memcpy(next.bssid, app.sett.bssid, sizeof(next.bssid));
    next.channel = app.sett.channel;

    bool meterTurnedOn = next.meterEnabled && !app.sett.meterEnabled;
    bool reboot = next.rfcEnabled != app.sett.rfcEnabled || next.rfcPort != app.sett.rfcPort;
    app.sett = next;
    storage::saveSettings(app.sett);
    Log.println("Настройки сохранены");
    if (meterTurnedOn) poller::onMeterEnabled();
    if (reboot) app.rebootNow.store(true);  // сервер RFC 2217 на ходу не перезапускается
}

// Канал и BSSID роутера после подключения — для быстрого коннекта (как в waterius).
void saveFastConnect() {
    uint8_t channel = 0;
    uint8_t bssid[6];
    if (!net::takeFastConnect(channel, bssid)) return;
    if (channel == app.sett.channel && memcmp(bssid, app.sett.bssid, sizeof(bssid)) == 0) return;
    app.sett.channel = channel;
    memcpy(app.sett.bssid, bssid, sizeof(bssid));
    storage::saveSettings(app.sett);
}

// ArduinoOTA — для pio run -t upload --upload-port <IP>. Стартует, когда появилась сеть.
void arduinoOta() {
    static bool started = false;
    if (!started) {
        if (!net::connected()) return;
        ArduinoOTA.setHostname(net::apName());
        ArduinoOTA.setMdnsEnabled(false);
        ArduinoOTA.begin();
        started = true;
    }
    ArduinoOTA.handle();
}

// Удержание BOOT 5 секунд — сброс к заводским настройкам.
void checkFactoryReset() {
    static uint32_t pressedAt = 0;
    if (digitalRead(BOOT_PIN) == HIGH) {
        pressedAt = 0;
        return;
    }
    if (pressedAt == 0) {
        pressedAt = millis() | 1;
        return;
    }
    if (millis() - pressedAt < FACTORY_RESET_HOLD_MS) return;
    Log.println("Сброс к заводским настройкам");
    storage::resetAll();
    delay(300);
    ESP.restart();
}

}  // namespace

void setup() {
    Log.begin(115200);
    delay(200);
    Log.printf("esp32-opto %s\n", FIRMWARE_VERSION);
    pinMode(BOOT_PIN, INPUT_PULLUP);

    storage::loadSettings(app.sett);
    app.hasReading = storage::loadLastReading(app.last, app.lastReadAt);
    app.otaError = storage::loadOtaError();

    bus.begin(app.sett.serial);
    net::begin(app.sett);
    web::begin();
    if (app.sett.rfcEnabled) rfc2217::begin(app.sett.rfcPort);
    poller::begin();
}

void loop() {
    net::loop(app.sett);
    saveFastConnect();
    wifi_portal::loop();
    web::loop();
    arduinoOta();
    rfc2217::loop();
    applyPendingSettings();
    poller::loop();
    checkFactoryReset();
    if (app.rebootNow.load()) {
        delay(300);
        ESP.restart();
    }
    delay(2);
}
```

- [ ] **Шаг 2: Сборка**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3 -e esp32-c3`
Expected: код возврата 0, оба env `SUCCESS`.

- [ ] **Шаг 3: `CLAUDE.md` — привести к реализации**

Правка 1 в `CLAUDE.md` — найти:

````
```sh
~/.platformio/penv/bin/pio run -e esp32-s3            # сборка
~/.platformio/penv/bin/pio run -e esp32-c3 -t upload  # прошивка
~/.platformio/penv/bin/pio test -e native             # юнит-тесты, без платы
~/.platformio/penv/bin/pio device monitor             # лог, 115200
```
````

заменить на:

````
```sh
~/.platformio/penv/bin/pio run -e esp32-s3 -e esp32-c3   # сборка обеих плат
~/.platformio/penv/bin/pio run -e esp32-s3 -t upload     # прошивка
~/.platformio/penv/bin/pio run -e esp32-s3 -t uploadfs   # веб-страницы из data/
~/.platformio/penv/bin/pio test -e native                # юнит-тесты, без платы
~/.platformio/penv/bin/pio device monitor                # лог, 115200
python3 tools/fake_cloud.py                              # заглушка облака Waterius
```
````

Правка 2 в `CLAUDE.md` — найти:

```
Тесты не лезут внутрь клиента
DLMS, поэтому при смене реализации меняется лишь `readMeter()` в тесте.
```

заменить на:

```
Тесты не лезут внутрь Gurux,
поэтому при смене реализации меняется лишь `readMeter()` в тесте.
```

Правка 3 в `CLAUDE.md` — найти:

```
src/core/    ядро, без Arduino: dlms (HDLC+COSEM), nartis (адаптер),
             cloud (тело запроса), meter.h, opto_port.h (интерфейсы)
src/port/    железо: opto_esp32 (UART1), net (Wi-Fi/HTTP), web (страница),
             rfc2217 (прозрачный serial), settings_nvs, hal_esp32
src/app.h    общее состояние (настройки, последнее чтение, статус облака)
tools/       nartis_probe.py — опрос счётчика с компьютера через головку на USB
docs/        исследование ИК-головки RIXUTECH на CP2102N и её доработки
```

заменить на:

```
src/core/     ядро, без Arduino: nartis (адаптер на GuruxDLMS.c), cloud (запрос
              в облако, разбор ota), settings.h, meter.h, opto_port.h
src/port/     железо: opto_bus (владелец UART1), opto_esp32, net (Wi-Fi, HTTPS),
              wifi_portal (перенос из waterius), web (API), rfc2217, ota_cloud,
              storage (NVS), log (USB + кольцевой буфер), hal_esp32
src/poller.*  автомат опроса счётчика и отправки в облако
src/app.h     общее состояние и флаги запросов с веб-страниц
data/         четыре страницы, app.js, style.css — образ LittleFS
lib/rfc2217-server/  копия igrr/rfc2217-server с патчем (см. PATCHES.md)
test/test_nartis/    юнит-тесты протокола: эмулятор счётчика и реальный дамп
tools/        nartis_probe.py — опрос счётчика с компьютера, fake_cloud.py —
              заглушка облака для проверки отправки и OTA
docs/         исследование ИК-головки RIXUTECH на CP2102N и её доработки
```

Правка 4 в `CLAUDE.md` — найти:

```
наследует `core::IMeter` рядом с `NartisMeter`.
```

заменить на:

```
наследует `core::IMeter` рядом с `NartisMeter`.

**Правило потоков.** Весь наш код работает в `loop()`. Веб-хендлеры (задача
`async_tcp`) и колбэки сервера RFC 2217 (его задачи) только читают `app` и
ставят атомарные флаги; UART, NVS, TLS и переключения Wi-Fi — только из
`loop()`. При любом изменении `core::Settings` поднять `SETTINGS_VERSION`.
Вывод — только через `Log` (`src/port/log.h`), не `Serial`: иначе сообщения
нет на странице лога.
```

Правка 5 в `CLAUDE.md` — найти:

```
  менять параметры COM-порта (скорость, биты, чётность, стоп-биты). Это
  должно работать и в локальной сети, и через удалённый сервер.
```

заменить на:

```
  менять параметры COM-порта (скорость, биты, чётность, стоп-биты). Сейчас
  только в локальной сети; доступ через удалённый сервер отложен (спека,
  «Вне рамок»).
```

Правка 6 в `CLAUDE.md` — найти:

```
- **Один оптопорт на двух потребителей.** Его делят периодический опрос
  адаптером и прозрачный режим. Нужен явный арбитраж: пока идёт
  прозрачная сессия, опрос откладывается. После сессии порт возвращается
  в формат адаптера, потому что пользователь мог поменять скорость и
  формат.
```

заменить на:

```
- **Один оптопорт на двух потребителей.** Его делят опрос и прозрачный
  режим через `OptoBus`. Подключение клиента RFC 2217 сразу прерывает
  идущий опрос (до следующего цикла); после отключения порт свободен, а
  параметры порта выставляет себе каждый опрос при старте.
```

Правка 7 в `CLAUDE.md` — найти:

```
  Z. Порядок попыток: сначала SNRM на 9600, при тишине — режим E.
```

заменить на:

```
  Z. Режим E не реализован (вне рамок спеки): прошивка сразу шлёт SNRM на 9600.
```

Правка 8 в `CLAUDE.md` — найти:

```
- **Для времени и версии ПО счётчика полей нет.** `fw` — это версия
  прошивки ESP. Их нужно передавать в дополнительных полях
  (сериализатор лишние поля игнорирует) или договориться о новых полях с
  бэкендом.
- Успех — только HTTP 200. JSON в ответе Waterius применяет как новые
  настройки: так сервер может менять параметры устройства.
```

заменить на:

```
- **Для времени и версии ПО счётчика полей нет.** `fw` — это версия
  прошивки ESP. Прошивка шлёт их дополнительными полями `meter_fw`,
  `meter_time`, `meter_read_at` вместе с кодом ошибки OTA `ota_error` —
  бэкенд их пока игнорирует.
- Успех — только HTTP 200. Из ответа прошивка разбирает только блок `ota`
  (OTA через сервер, как в waterius); выдачу его для `/api/source/iz/`
  владелец добавит в бэкенд отдельно.
```

Правка 9 в `CLAUDE.md` — найти:

```
- **Три веб-страницы**: статус, настройки (счётчик, порт, облако), Wi-Fi.
```

заменить на:

```
- **Четыре веб-страницы**: статус, настройки (счётчик, порт, облако), Wi-Fi,
  лог в реальном времени с байтами оптопорта (кольцевой буфер 16 КБ).
```

- [ ] **Шаг 4: `README.md` (заменить целиком)**

````markdown
# esp32-opto

Прошивка для чтения электросчётчика **НАРТИС-100** через оптопорт на
**ESP32-S3** и **ESP32-C3** с отправкой показаний в облако Waterius.

- чтение по СПОДЭС (DLMS/COSEM поверх HDLC) библиотекой GuruxDLMS.c;
- веб-морда: статус, настройки, Wi-Fi (портал перенесён из прошивки Waterius),
  лог в реальном времени;
- прозрачный serial по RFC 2217 в локальной сети;
- OTA: страница `/update`, ArduinoOTA, сервер Waterius.

Оптоголовка — доработанный кабель RIXUTECH на CP2102N, её разбор — в
[docs/](docs/README.md). Дизайн прошивки —
[docs/superpowers/specs/2026-09-17-esp32-opto-firmware-design.md](docs/superpowers/specs/2026-09-17-esp32-opto-firmware-design.md).

## Сборка и прошивка

```sh
~/.platformio/penv/bin/pio run -e esp32-s3 -t upload     # прошивка
~/.platformio/penv/bin/pio run -e esp32-s3 -t uploadfs   # веб-страницы
```

Для ESP32-C3 — то же с `-e esp32-c3`.

## Первое включение

1. Подключиться к сети Wi-Fi `esp32-opto-XXXX` — страница настройки откроется
   сама, иначе `http://192.168.4.1/wifi.html`.
2. Выбрать роутер и ввести пароль.
3. Адрес устройства в сети роутера — в списке клиентов роутера или в USB-логе.
   На `/settings` задать ключ и e-mail облака Waterius.

Сброс к заводским настройкам — удержать кнопку BOOT 5 секунд.

## Подключение головки

| Сигнал | ESP32-S3 | ESP32-C3 |
|---|---|---|
| RX (с головки) | GPIO17 | GPIO4 |
| TX (на головку) | GPIO18 | GPIO5 |

Питание головки и доработка платы — в [docs/02-rixutech-cp2102n-teardown.md](docs/02-rixutech-cp2102n-teardown.md).
````

- [ ] **Шаг 5: На железе (владелец) — сброс и итоговая проверка**

1. Удержать BOOT 5 секунд → в логе `Сброс к заводским настройкам`, перезагрузка, точка доступа `esp32-opto-XXXX`, на `/settings` умолчания.
2. Итоговая проверка по спеке (раздел 12), по порядку: AP и captive portal → подключение к роутеру → четыре страницы, в логе новые строки без перезагрузки страницы → чтение счётчика → облако отвечает 200 → клиент RFC 2217 прерывает опрос → OTA прошивки и LittleFS через `/update`. Для C3 повторить шаги с `-e esp32-c3` (головка на GPIO4 / GPIO5).

- [ ] **Шаг 6: Коммит**

```bash
git add src/main.cpp CLAUDE.md README.md
git commit -m "feat: сброс кнопкой BOOT; docs: CLAUDE.md и README под реализацию"
```
