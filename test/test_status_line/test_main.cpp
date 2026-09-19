// Юнит-тесты строки состояния (src/core/status_line.h).
//
// Строку читают двое: человек в мониторе и скрипт. Отсюда всё, что здесь
// проверяется, — кавычки у текстовых полей (чтобы разбор шёл одним регекспом),
// экранирование, замена переводов строки (иначе строка развалится надвое) и
// обрезка длинного текста по границе буквы: в этом проекте уже была ошибка
// «адрес 16, ко?» — оборванная посреди кириллической буквы строка.
//
//   ~/.platformio/penv/bin/pio test -e native -f test_status_line
#include <string.h>
#include <unity.h>

#include "core/status_line.h"

using core::StatusFacts;
using core::WifiMode;

void setUp() {}
void tearDown() {}

// Типовые факты живой платы: от них отталкиваются остальные тесты.
static StatusFacts typical() {
    StatusFacts f;
    f.version = "0.2.0";
    f.uptimeS = 3725;
    f.heap = 142312;
    f.bootReason = "сторож главного цикла";
    f.safeMode = false;
    f.wifi = WifiMode::ApStation;
    f.apName = "esp32-opto-926C";
    f.apChannel = 5;
    f.apCfgChannel = 5;
    f.apClients = 1;
    f.ssid = "Дача";
    f.ip = "192.168.1.42";
    f.rssi = -67;
    f.drops = 3;
    f.offlineS = 0;
    f.busOwner = "transparent";
    f.rfcClient = true;
    f.hasReading = true;
    f.total = 12345.678;
    f.nextReadS = 284;
    f.cloudCode = 200;
    f.nextSendS = 284;
    return f;
}

// Текст остался целым UTF-8: ни одной оборванной буквы.
static bool validUtf8(const char* text) {
    const unsigned char* p = (const unsigned char*)text;
    while (*p) {
        size_t n = *p < 0x80              ? 1
                   : (*p & 0xE0) == 0xC0  ? 2
                   : (*p & 0xF0) == 0xE0  ? 3
                   : (*p & 0xF8) == 0xF0  ? 4
                                          : 0;
        if (!n) return false;
        for (size_t i = 1; i < n; ++i)
            if ((p[i] & 0xC0) != 0x80) return false;
        p += n;
    }
    return true;
}

// Кавычки расставлены парами и строка разбирается как key=value.
static bool quotesBalanced(const char* text) {
    int open = 0;
    for (const char* p = text; *p; ++p) {
        if (*p == '\\' && p[1]) {
            ++p;
            continue;
        }
        if (*p == '"') open ^= 1;
    }
    return open == 0;
}

void test_typical_line() {
    char out[core::STATUS_CAP];
    StatusFacts f = typical();
    size_t len = core::formatStatus(f, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(
        "status fw=0.2.0 up=3725 heap=142312 boot=\"сторож главного цикла\" safe=0 "
        "wifi=ap+sta ap=\"esp32-opto-926C\" apch=5 apcfg=5 apcli=1 ssid=\"Дача\" ip=192.168.1.42 rssi=-67 drops=3 offline=0 "
        "bus=transparent port=9600-8N1 rfc=1 read=1 total=12345.678 merr=\"\" mnext=284 "
        "code=200 cerr=\"\" cnext=284",
        out);
    TEST_ASSERT_EQUAL(strlen(out), len);
}

// Пустые факты тоже должны разбираться: ни одного поля без значения.
void test_empty_facts_still_parse() {
    char out[core::STATUS_CAP];
    StatusFacts f;
    core::formatStatus(f, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(
        "status fw=- up=0 heap=0 boot=\"\" safe=0 wifi=off ap=\"\" apch=0 apcfg=0 apcli=0 ssid=\"\" ip=- rssi=0 drops=0 "
        "offline=0 bus=- port=9600-8N1 rfc=0 read=0 total=0.000 merr=\"\" mnext=0 "
        "code=0 cerr=\"\" cnext=0",
        out);
}

void test_missing_ip_does_not_shift_fields() {
    char out[core::STATUS_CAP];
    StatusFacts f = typical();
    f.ip = "";
    core::formatStatus(f, out, sizeof(out));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, " ip=- rssi="), "пустой адрес сдвинул поля");
}

void test_quote_and_backslash_in_ssid_are_escaped() {
    char out[core::STATUS_CAP];
    StatusFacts f = typical();
    f.ssid = "Дом \"2,4\" \\ гость";
    core::formatStatus(f, out, sizeof(out));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "ssid=\"Дом \\\"2,4\\\" \\\\ гость\" ip="),
                                 "кавычка в имени сети не экранирована");
    TEST_ASSERT_TRUE(quotesBalanced(out));
}

// Перевод строки внутри ошибки разорвал бы строку состояния надвое.
void test_control_bytes_become_spaces() {
    char out[core::STATUS_CAP];
    StatusFacts f = typical();
    f.meterError = "нет связи\nкод 253\tповтор";
    core::formatStatus(f, out, sizeof(out));
    TEST_ASSERT_NULL_MESSAGE(strchr(out, '\n'), "перевод строки разорвал строку состояния");
    TEST_ASSERT_NULL(strchr(out, '\t'));
    TEST_ASSERT_NOT_NULL(strstr(out, "merr=\"нет связи код 253 повтор\""));
}

// Ровно та ошибка, что была в проекте: обрыв посреди кириллической буквы.
void test_long_error_is_cut_on_a_letter_boundary() {
    char error[200];
    error[0] = 'x';  // сдвигаем кириллицу так, чтобы предел пришёлся на середину буквы
    size_t pos = 1;
    while (pos < sizeof(error) - 3) {
        error[pos++] = (char)0xD0;
        error[pos++] = (char)0xBA;  // «к»
    }
    error[pos] = 0;

    char out[core::STATUS_CAP];
    StatusFacts f = typical();
    f.meterError = error;
    core::formatStatus(f, out, sizeof(out));
    TEST_ASSERT_TRUE_MESSAGE(validUtf8(out), "строку оборвали посреди буквы");
    TEST_ASSERT_TRUE_MESSAGE(quotesBalanced(out), "обрезка съела закрывающую кавычку");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, " cnext="), "после обрезки пропал хвост строки");
}

// Все поля максимальной длины обязаны помещаться без укорачивания.
void test_worst_case_fits_into_cap() {
    char ssid[64], boot[128], merr[160], cerr[128];
    memset(ssid, 'S', sizeof(ssid));
    ssid[core::STATUS_SSID_CAP] = 0;
    memset(boot, 'B', sizeof(boot));
    boot[core::STATUS_BOOT_CAP] = 0;
    memset(merr, 'M', sizeof(merr));
    merr[core::STATUS_METER_ERROR_CAP] = 0;
    memset(cerr, 'C', sizeof(cerr));
    cerr[core::STATUS_CLOUD_ERROR_CAP] = 0;

    char out[core::STATUS_CAP];
    StatusFacts f = typical();
    f.ssid = ssid;
    f.bootReason = boot;
    f.meterError = merr;
    f.cloudError = cerr;
    f.uptimeS = 0xFFFFFFFF;
    f.heap = 0xFFFFFFFF;
    f.drops = 0xFFFFFFFF;
    f.offlineS = 0xFFFFFFFF;
    f.nextReadS = 0xFFFFFFFF;
    f.nextSendS = 0xFFFFFFFF;
    f.cloudCode = -32768;
    f.total = 99999999.999;
    size_t len = core::formatStatus(f, out, sizeof(out));

    TEST_ASSERT_TRUE_MESSAGE(len < core::STATUS_CAP - 1, "худший случай упёрся в границу буфера");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, merr), "ошибку счётчика укоротили при худших полях");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, " cnext=4294967295"), "хвост строки не поместился");
}

// Даже в заведомо маленький буфер строка выходит завершённой и разборчивой.
void test_tiny_buffer_never_overflows() {
    StatusFacts f = typical();
    f.ssid = "Очень длинное имя сети для проверки";
    for (size_t cap = 1; cap <= 200; ++cap) {
        char out[256];
        memset(out, '#', sizeof(out));
        size_t len = core::formatStatus(f, out, cap);
        TEST_ASSERT_TRUE_MESSAGE(len < cap, "длина вышла за отведённый буфер");
        TEST_ASSERT_EQUAL_MESSAGE(0, out[len], "строку не завершили нулём");
        TEST_ASSERT_EQUAL_MESSAGE('#', out[cap], "запись вышла за границу буфера");
        TEST_ASSERT_TRUE_MESSAGE(quotesBalanced(out), "обрезанная строка с незакрытой кавычкой");
        TEST_ASSERT_TRUE_MESSAGE(validUtf8(out), "обрезка пришлась на середину буквы");
    }
}

void test_wifi_modes_are_distinct_tokens() {
    TEST_ASSERT_EQUAL_STRING("off", core::wifiModeName(WifiMode::Down));
    TEST_ASSERT_EQUAL_STRING("search", core::wifiModeName(WifiMode::Connecting));
    TEST_ASSERT_EQUAL_STRING("sta", core::wifiModeName(WifiMode::Station));
    TEST_ASSERT_EQUAL_STRING("ap", core::wifiModeName(WifiMode::Ap));
    TEST_ASSERT_EQUAL_STRING("ap+sta", core::wifiModeName(WifiMode::ApStation));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_typical_line);
    RUN_TEST(test_empty_facts_still_parse);
    RUN_TEST(test_missing_ip_does_not_shift_fields);
    RUN_TEST(test_quote_and_backslash_in_ssid_are_escaped);
    RUN_TEST(test_control_bytes_become_spaces);
    RUN_TEST(test_long_error_is_cut_on_a_letter_boundary);
    RUN_TEST(test_worst_case_fits_into_cap);
    RUN_TEST(test_tiny_buffer_never_overflows);
    RUN_TEST(test_wifi_modes_are_distinct_tokens);
    return UNITY_END();
}
