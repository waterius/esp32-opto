// Ядро: однострочный снимок состояния прошивки.
//
// Зачем. На обеих платах вывод идёт в USB Serial/JTAG, и всё напечатанное до
// подключения хоста драйвер выбрасывает. Подключаешь плату в середине работы —
// тишина: что с ней, определить нечем. Поэтому прошивка рассказывает о себе
// сама, раз в пять секунд, одной строкой.
//
// Строку читает и человек, и скрипт, поэтому формат — ключ=значение, порядок
// полей фиксированный, ключи латиницей (grep по дампу USB не должен зависеть от
// локали терминала), а значения остаются русскими: их читают глазами.
//
// Сборка строки — решение, а не исполнение: кавычки, экранирование и обрезка
// текста по границе буквы проверяются юнит-тестом на компьютере.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "opto_port.h"

namespace core {

// Худший реальный случай — около 560 байт. Поля, которым не хватит места,
// укорачиваются по границе буквы, но строка остаётся разборчивой.
const size_t STATUS_CAP = 640;

// Пределы текстовых полей: строка укладывается в STATUS_CAP арифметикой, а не
// обрезкой целиком — обрезка целиком оставила бы незакрытую кавычку.
const size_t STATUS_SSID_CAP = 32;
const size_t STATUS_BOOT_CAP = 72;
const size_t STATUS_METER_ERROR_CAP = 96;
const size_t STATUS_CLOUD_ERROR_CAP = 64;

// «STA подключён» и «STA ищет» — это решение, а не название: драйвер их не
// различает, и порт сам бы их перепутал. Поэтому здесь enum, а не строка.
enum class WifiMode : uint8_t {
    Down,        // SSID не задан или сеть выключена
    Connecting,  // STA ищет сеть
    Station,     // STA подключён
    Ap,          // только точка доступа
    ApStation,   // точка доступа и подключённый STA
};

struct StatusFacts {
    // кто мы и как себя чувствуем
    const char* version = "";
    uint32_t uptimeS = 0;
    uint32_t heap = 0;
    const char* bootReason = "";
    bool safeMode = false;

    // сеть
    WifiMode wifi = WifiMode::Down;
    const char* ssid = "";
    const char* ip = "";  // пусто — адреса нет
    int rssi = 0;
    uint32_t drops = 0;
    uint32_t offlineS = 0;

    // оптопорт
    const char* busOwner = "";  // free / meter / transparent
    SerialCfg port;
    bool rfcClient = false;

    // счётчик
    bool hasReading = false;
    double total = 0;
    const char* meterError = "";  // пусто — последнее чтение без ошибок
    uint32_t nextReadS = 0;

    // облако
    int cloudCode = 0;            // HTTP-код; < 0 — нет соединения; 0 — не отправляли
    const char* cloudError = "";  // пусто — последняя отправка удалась
    uint32_t nextSendS = 0;
};

const char* wifiModeName(WifiMode mode);

// Пишет строку без завершающего перевода строки, ставит '\0'.
// Возвращает длину без '\0'.
size_t formatStatus(const StatusFacts& facts, char* out, size_t cap);

}  // namespace core
