// Юнит-тесты записи настроек (src/core/settings_io.cpp, src/core/settings_v4.h).
//
// Главное здесь — что обновление прошивки не стоит ни одной настройки. Запись
// самоописательная: новое поле берёт умолчание, удалённое игнорируется, версия
// не двигается. Отдельно проверяется разовый перенос старого блоба версии 4 —
// по золотым байтам, снятым с настроенной платы.
//
//   ~/.platformio/penv/bin/pio test -e native -f test_settings
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "core/settings_io.h"
#include "core/settings_v4.h"

using core::LoadResult;
using core::Settings;
using core::settingsFromJson;
using core::settingsToJson;

// Unity сравнивает целые числа: enum class сам к ним не приводится.
#define ASSERT_RESULT(want, got, msg) TEST_ASSERT_EQUAL_INT_MESSAGE((int)(want), (int)(got), msg)

void setUp() {}
void tearDown() {}

namespace {

// Настройки, где ни одно поле не равно умолчанию: так пропажа поля при
// сериализации видна, а не маскируется совпадением с умолчанием.
Settings filled() {
    Settings s;
    snprintf(s.ssid, sizeof(s.ssid), "%s", "МояСеть \"дом\"");
    snprintf(s.pass, sizeof(s.pass), "%s", "pa\\ss\"word");
    for (uint8_t i = 0; i < 6; ++i) s.bssid[i] = (uint8_t)(0xa0 + i);
    s.channel = 11;
    s.ip = 0xC0A80132;
    s.gateway = 0xC0A80101;
    s.mask = 0xFFFFFF00;
    s.dns = 0x08080808;
    s.rebootMin = 30;
    s.serial.baud = 19200;
    s.serial.bits = 7;
    s.serial.parity = 'E';
    s.serial.stop = 2;
    s.meterEnabled = false;
    s.meterAddr = 17;
    snprintf(s.meterPwd, sizeof(s.meterPwd), "%s", "sekret");
    s.periodMin = 15;
    snprintf(s.host, sizeof(s.host), "%s", "http://192.168.1.2:8000");
    snprintf(s.key, sizeof(s.key), "%s", "KEY-1234");
    snprintf(s.email, sizeof(s.email), "%s", "a@b.ru");
    s.totalType = core::DT_ELECTRICITY;
    s.tariffType[0] = core::DT_DAY;
    s.tariffType[1] = core::DT_NIGHT;
    s.tariffType[2] = core::DT_PEAK;
    s.tariffType[3] = core::DT_HALF_PEAK;
    s.rfcEnabled = false;
    s.rfcPort = 2300;
    return s;
}

void assertSame(const Settings& a, const Settings& b) {
    TEST_ASSERT_EQUAL_STRING(a.ssid, b.ssid);
    TEST_ASSERT_EQUAL_STRING(a.pass, b.pass);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a.bssid, b.bssid, sizeof(a.bssid));
    TEST_ASSERT_EQUAL_UINT8(a.channel, b.channel);
    TEST_ASSERT_EQUAL_UINT32(a.ip, b.ip);
    TEST_ASSERT_EQUAL_UINT32(a.gateway, b.gateway);
    TEST_ASSERT_EQUAL_UINT32(a.mask, b.mask);
    TEST_ASSERT_EQUAL_UINT32(a.dns, b.dns);
    TEST_ASSERT_EQUAL_UINT16(a.rebootMin, b.rebootMin);
    TEST_ASSERT_EQUAL_UINT32(a.serial.baud, b.serial.baud);
    TEST_ASSERT_EQUAL_UINT8(a.serial.bits, b.serial.bits);
    TEST_ASSERT_EQUAL_CHAR(a.serial.parity, b.serial.parity);
    TEST_ASSERT_EQUAL_UINT8(a.serial.stop, b.serial.stop);
    TEST_ASSERT_EQUAL(a.meterEnabled, b.meterEnabled);
    TEST_ASSERT_EQUAL_UINT8(a.meterAddr, b.meterAddr);
    TEST_ASSERT_EQUAL_STRING(a.meterPwd, b.meterPwd);
    TEST_ASSERT_EQUAL_UINT16(a.periodMin, b.periodMin);
    TEST_ASSERT_EQUAL_STRING(a.host, b.host);
    TEST_ASSERT_EQUAL_STRING(a.key, b.key);
    TEST_ASSERT_EQUAL_STRING(a.email, b.email);
    TEST_ASSERT_EQUAL_INT8(a.totalType, b.totalType);
    TEST_ASSERT_EQUAL_INT8_ARRAY(a.tariffType, b.tariffType, core::MAX_TARIFFS);
    TEST_ASSERT_EQUAL(a.rfcEnabled, b.rfcEnabled);
    TEST_ASSERT_EQUAL_UINT16(a.rfcPort, b.rfcPort);
}

// Тот же буфер, что и у записи в NVS (storage::CFG_CAP). Предельные настройки
// со сплошными кавычками занимают 971 байт — запас на случай, если в строку
// попадёт что-то ещё более дорогое в экранировании.
const size_t CAP = 1536;

}  // namespace

// --- круговой обход --------------------------------------------------------

void test_every_field_survives_the_round_trip() {
    Settings s = filled();
    char json[CAP];
    size_t n = settingsToJson(s, json, sizeof(json));
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, n, "настройки не сериализовались");

    Settings back;
    ASSERT_RESULT(LoadResult::Ok, settingsFromJson(json, n, back), "не тот исход загрузки");
    assertSame(s, back);
}

// Буфер записи должен вмещать самые длинные настройки, какие вообще можно
// ввести на странице: не влезли — saveSettings() не сохранит ничего.
void test_longest_possible_settings_fit_the_buffer() {
    Settings s = filled();
    memset(s.ssid, '"', sizeof(s.ssid) - 1);   // кавычка — худший случай: экранируется
    memset(s.pass, '"', sizeof(s.pass) - 1);
    memset(s.meterPwd, '"', sizeof(s.meterPwd) - 1);
    memset(s.host, '"', sizeof(s.host) - 1);
    memset(s.key, '"', sizeof(s.key) - 1);
    memset(s.email, '"', sizeof(s.email) - 1);
    s.serial.baud = 115200;
    s.rebootMin = 1440;
    s.periodMin = 1440;
    s.rfcPort = 65535;
    s.channel = 255;
    s.meterAddr = 255;

    char json[CAP];
    size_t n = settingsToJson(s, json, sizeof(json));
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, n, "предельные настройки не влезли в буфер");

    Settings back;
    ASSERT_RESULT(LoadResult::Ok, settingsFromJson(json, n, back), "не тот исход загрузки");
    assertSame(s, back);
}

void test_small_buffer_yields_zero() {
    char json[64];
    TEST_ASSERT_EQUAL_MESSAGE(0, settingsToJson(filled(), json, sizeof(json)),
                              "обрезанная запись выдана за готовую");
}

// --- чего в записи нет -----------------------------------------------------

void test_missing_key_falls_back_to_default_and_neighbours_survive() {
    const char* json = "{\"v\":4,\"key\":\"abc\",\"rfc_port\":2300}";
    Settings s;
    ASSERT_RESULT(LoadResult::Ok, settingsFromJson(json, strlen(json), s), "не тот исход загрузки");

    Settings d;  // умолчания
    TEST_ASSERT_EQUAL_STRING_MESSAGE(d.host, s.host, "пропавший ключ не взял умолчание");
    TEST_ASSERT_EQUAL_UINT16(d.periodMin, s.periodMin);
    TEST_ASSERT_EQUAL_STRING(d.meterPwd, s.meterPwd);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("abc", s.key, "соседний ключ не прочитан");
    TEST_ASSERT_EQUAL_UINT16(2300, s.rfcPort);
}

void test_unknown_key_is_ignored() {
    const char* json = "{\"v\":4,\"key\":\"abc\",\"был_такой_ключ\":42,\"nested\":{\"a\":1}}";
    Settings s;
    ASSERT_RESULT(LoadResult::Ok, settingsFromJson(json, strlen(json), s), "не тот исход загрузки");
    TEST_ASSERT_EQUAL_STRING("abc", s.key);
}

void test_garbage_gives_defaults() {
    Settings d;
    const char* cases[] = {"", "{", "не json", "[1,2,3]", "42", "null"};
    for (const char* c : cases) {
        Settings s = filled();
        ASSERT_RESULT(LoadResult::Defaults, settingsFromJson(c, strlen(c), s), c);
        assertSame(d, s);
    }
    Settings s = filled();
    ASSERT_RESULT(LoadResult::Defaults, settingsFromJson("{\"v\":4}", 0, s), "не тот исход загрузки");
    assertSame(d, s);
    s = filled();
    ASSERT_RESULT(LoadResult::Defaults, settingsFromJson(nullptr, 10, s), "не тот исход загрузки");
    assertSame(d, s);
}

// Значение не того типа или вне диапазона — поле остаётся с умолчанием, а не
// становится мусором.
void test_out_of_range_value_does_not_touch_the_field() {
    const char* json = "{\"v\":4,\"baud\":-5,\"bits\":\"восемь\",\"parity\":\"Q\",\"rfc_port\":99999}";
    Settings s, d;
    ASSERT_RESULT(LoadResult::Ok, settingsFromJson(json, strlen(json), s), "не тот исход загрузки");
    TEST_ASSERT_EQUAL_UINT32(d.serial.baud, s.serial.baud);
    TEST_ASSERT_EQUAL_UINT8(d.serial.bits, s.serial.bits);
    TEST_ASSERT_EQUAL_CHAR(d.serial.parity, s.serial.parity);
    TEST_ASSERT_EQUAL_UINT16(d.rfcPort, s.rfcPort);
}

void test_long_string_is_truncated_not_overflowed() {
    char json[512];
    char host[201];
    memset(host, 'x', sizeof(host) - 1);
    host[sizeof(host) - 1] = 0;
    snprintf(json, sizeof(json), "{\"v\":4,\"host\":\"%s\"}", host);

    Settings s;
    ASSERT_RESULT(LoadResult::Ok, settingsFromJson(json, strlen(json), s), "не тот исход загрузки");
    TEST_ASSERT_EQUAL_UINT(sizeof(s.host) - 1, strlen(s.host));
    TEST_ASSERT_EQUAL_MESSAGE(0, memcmp(s.host, host, sizeof(s.host) - 1), "обрезано не то");
}

// --- версии ----------------------------------------------------------------

void test_record_from_the_future_is_read_without_upgrades() {
    char json[256];
    snprintf(json, sizeof(json), "{\"v\":%u,\"key\":\"abc\",\"ssid\":\"MyNet\",\"поле_из_будущего\":1}",
             (unsigned)(core::SETTINGS_VERSION + 1));
    Settings s;
    ASSERT_RESULT(LoadResult::FromFuture, settingsFromJson(json, strlen(json), s), "не тот исход загрузки");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("abc", s.key, "откат прошивки потерял ключ облака");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("MyNet", s.ssid, "откат прошивки потерял Wi-Fi");
}

void test_older_record_goes_through_the_chain() {
    char json[256];
    snprintf(json, sizeof(json), "{\"v\":%u,\"key\":\"abc\"}",
             (unsigned)(core::SETTINGS_VERSION - 1));
    Settings s;
    ASSERT_RESULT(LoadResult::Migrated, settingsFromJson(json, strlen(json), s), "не тот исход загрузки");
    TEST_ASSERT_EQUAL_STRING("abc", s.key);

    // storage сохранит результат заново — в записи должна оказаться текущая версия
    char out[CAP];
    size_t n = settingsToJson(s, out, sizeof(out));
    TEST_ASSERT_GREATER_THAN(0, n);
    char expect[24];
    snprintf(expect, sizeof(expect), "\"v\":%u", core::SETTINGS_VERSION);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, expect), "версия в записи не обновилась");

    Settings back;
    ASSERT_RESULT(LoadResult::Ok, settingsFromJson(out, n, back), "не тот исход загрузки");
}

void test_record_without_version_is_migrated() {
    const char* json = "{\"key\":\"abc\"}";
    Settings s;
    ASSERT_RESULT(LoadResult::Migrated, settingsFromJson(json, strlen(json), s), "не тот исход загрузки");
    TEST_ASSERT_EQUAL_STRING("abc", s.key);
}

// --- перенос блоба версии 4 ------------------------------------------------

namespace {

// Золотые байты записи `settings`, какую клали прошивки до перехода на JSON:
// ssid MyNet, pass s3cret, BSSID a0:b1:c2:d3:e4:f5, канал 11, статический
// адрес, 19200 7E2, опрос выключен, адрес счётчика 16, пароль sekret, период
// 15 минут, старый хост облака, тарифы 5 и 6, порт RFC 2300.
// Порядок байт у чисел — little-endian: он общий у x86-64, xtensa и riscv32.
const uint8_t V4_GOLD[] = {
    0x04, 0x00, 0x4d, 0x79, 0x4e, 0x65, 0x74, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x73,
    0x33, 0x63, 0x72, 0x65, 0x74, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xa0, 0xb1, 0xc2, 0xd3, 0xe4, 0xf5, 0x0b, 0x00,
    0x32, 0x01, 0xa8, 0xc0, 0x01, 0x01, 0xa8, 0xc0, 0x00, 0xff, 0xff, 0xff,
    0x08, 0x08, 0x08, 0x08, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x4b, 0x00, 0x00,
    0x07, 0x45, 0x02, 0x00, 0x00, 0x10, 0x73, 0x65, 0x6b, 0x72, 0x65, 0x74,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0f, 0x00, 0x68, 0x74, 0x74, 0x70, 0x73, 0x3a, 0x2f, 0x2f, 0x63, 0x6c,
    0x6f, 0x75, 0x64, 0x2e, 0x77, 0x61, 0x74, 0x65, 0x72, 0x69, 0x75, 0x73,
    0x2e, 0x72, 0x75, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4b, 0x45, 0x59, 0x2d, 0x31, 0x32,
    0x33, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x61,
    0x40, 0x62, 0x2e, 0x72, 0x75, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x02, 0x05, 0x06, 0xff, 0xff, 0x01, 0x00, 0xfc, 0x08,
};

}  // namespace

void test_golden_v4_blob_becomes_settings() {
    TEST_ASSERT_EQUAL_MESSAGE(sizeof(core::SettingsV4), sizeof(V4_GOLD),
                              "золотая запись не того размера");
    core::SettingsV4 v4;
    memcpy(&v4, V4_GOLD, sizeof(v4));
    TEST_ASSERT_EQUAL_UINT16(4, v4.version);

    Settings s;
    core::fromV4(v4, s);
    TEST_ASSERT_EQUAL_STRING("MyNet", s.ssid);
    TEST_ASSERT_EQUAL_STRING("s3cret", s.pass);
    const uint8_t bssid[6] = {0xa0, 0xb1, 0xc2, 0xd3, 0xe4, 0xf5};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bssid, s.bssid, sizeof(bssid));
    TEST_ASSERT_EQUAL_UINT8(11, s.channel);
    TEST_ASSERT_EQUAL_HEX32(0xC0A80132, s.ip);
    TEST_ASSERT_EQUAL_HEX32(0xC0A80101, s.gateway);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFF00, s.mask);
    TEST_ASSERT_EQUAL_HEX32(0x08080808, s.dns);
    TEST_ASSERT_EQUAL_UINT16(30, s.rebootMin);
    TEST_ASSERT_EQUAL_UINT32(19200, s.serial.baud);
    TEST_ASSERT_EQUAL_UINT8(7, s.serial.bits);
    TEST_ASSERT_EQUAL_CHAR('E', s.serial.parity);
    TEST_ASSERT_EQUAL_UINT8(2, s.serial.stop);
    TEST_ASSERT_FALSE(s.meterEnabled);
    TEST_ASSERT_EQUAL_UINT8(16, s.meterAddr);
    TEST_ASSERT_EQUAL_STRING("sekret", s.meterPwd);
    TEST_ASSERT_EQUAL_UINT16(15, s.periodMin);
    TEST_ASSERT_EQUAL_STRING("https://cloud.waterius.ru", s.host);
    TEST_ASSERT_EQUAL_STRING("KEY-1234", s.key);
    TEST_ASSERT_EQUAL_STRING("a@b.ru", s.email);
    TEST_ASSERT_EQUAL_INT8(core::DT_ELECTRICITY, s.totalType);
    TEST_ASSERT_EQUAL_INT8(core::DT_DAY, s.tariffType[0]);
    TEST_ASSERT_EQUAL_INT8(core::DT_NIGHT, s.tariffType[1]);
    TEST_ASSERT_EQUAL_INT8(core::DT_NONE, s.tariffType[2]);
    TEST_ASSERT_EQUAL_INT8(core::DT_NONE, s.tariffType[3]);
    TEST_ASSERT_TRUE(s.rfcEnabled);
    TEST_ASSERT_EQUAL_UINT16(2300, s.rfcPort);
}

// Перенос идёт через ту же запись, что потом ляжет в NVS: проверяем всю
// дорогу целиком, а не только первую её половину.
void test_migrated_v4_survives_the_new_record() {
    core::SettingsV4 v4;
    memcpy(&v4, V4_GOLD, sizeof(v4));
    Settings s;
    core::fromV4(v4, s);

    char json[CAP];
    size_t n = settingsToJson(s, json, sizeof(json));
    TEST_ASSERT_GREATER_THAN(0, n);
    Settings back;
    ASSERT_RESULT(LoadResult::Ok, settingsFromJson(json, n, back), "не тот исход загрузки");
    assertSame(s, back);
}

// Строка без нуля в конце (запись побилась) не утаскивает соседние поля.
void test_unterminated_string_in_v4_is_clipped() {
    core::SettingsV4 v4;
    memcpy(&v4, V4_GOLD, sizeof(v4));
    memset(v4.ssid, 'x', sizeof(v4.ssid));
    Settings s;
    core::fromV4(v4, s);
    TEST_ASSERT_EQUAL_UINT(sizeof(v4.ssid) - 1, strlen(s.ssid));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("s3cret", s.pass, "строка без нуля съела соседа");
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_every_field_survives_the_round_trip);
    RUN_TEST(test_longest_possible_settings_fit_the_buffer);
    RUN_TEST(test_small_buffer_yields_zero);
    RUN_TEST(test_missing_key_falls_back_to_default_and_neighbours_survive);
    RUN_TEST(test_unknown_key_is_ignored);
    RUN_TEST(test_garbage_gives_defaults);
    RUN_TEST(test_out_of_range_value_does_not_touch_the_field);
    RUN_TEST(test_long_string_is_truncated_not_overflowed);
    RUN_TEST(test_record_from_the_future_is_read_without_upgrades);
    RUN_TEST(test_older_record_goes_through_the_chain);
    RUN_TEST(test_record_without_version_is_migrated);
    RUN_TEST(test_golden_v4_blob_becomes_settings);
    RUN_TEST(test_migrated_v4_survives_the_new_record);
    RUN_TEST(test_unterminated_string_in_v4_is_clipped);
    return UNITY_END();
}
