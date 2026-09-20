// Юнит-тесты тела запроса в облако (src/core/cloud.cpp).
//
// Главное здесь — что уходит, а что нет: коды data_type выбирает человек, и
// пока он не выбрал, показаний в запросе быть не должно. Нуль вместо показания
// облако принять не откажется — он просто станет расходом.
//
//   ~/.platformio/penv/bin/pio test -e native -f test_cloud
#include <ArduinoJson.h>
#include <unity.h>

#include "core/cloud.h"

using core::buildCloudPayload;
using core::DeviceInfo;
using core::MeterData;
using core::Settings;

void setUp() {}
void tearDown() {}

namespace {

// Счётчик прочитан целиком: сумма и четыре тарифа.
MeterData reading() {
    MeterData m;
    m.total = 1000;
    m.tariff[0] = 100;
    m.tariff[1] = 200;
    m.tariff[2] = 300;
    m.tariff[3] = 400;
    m.tariffCount = 4;
    snprintf(m.serial, sizeof(m.serial), "%s", "12345678");
    return m;
}

Settings settings() {
    Settings s;
    snprintf(s.key, sizeof(s.key), "%s", "abcd1234");
    return s;
}

// Собирает запрос и разбирает его обратно — проверяем поля, а не текст JSON.
void build(const MeterData& m, const Settings& s, JsonDocument& out) {
    char body[896];
    DeviceInfo dev;
    dev.fw = "0.2.0";
    dev.ip = "192.168.1.50";
    size_t n = buildCloudPayload(m, 1758000000, s, dev, body, sizeof(body));
    TEST_ASSERT_TRUE_MESSAGE(n > 0, "тело запроса не собралось");
    TEST_ASSERT_EQUAL_MESSAGE(n, strlen(body), "длина не совпала с содержимым");
    TEST_ASSERT_FALSE_MESSAGE(deserializeJson(out, body), "получился не JSON");
}

}  // namespace

// --- умолчания -------------------------------------------------------------

void test_nothing_is_sent_until_the_user_picks_types() {
    JsonDocument doc;
    build(reading(), settings(), doc);

    TEST_ASSERT_FALSE_MESSAGE(doc["total"].is<double>(), "сумма ушла без выбора типа");
    TEST_ASSERT_FALSE_MESSAGE(doc["data_type"].is<int>(), "код суммы ушёл без выбора типа");
    for (uint8_t i = 1; i <= core::MAX_TARIFFS; ++i) {
        char name[12];
        snprintf(name, sizeof(name), "total%u", i);
        TEST_ASSERT_FALSE_MESSAGE(doc[name].is<double>(), "тариф ушёл без выбора типа");
    }
}

// Служебные поля от выбора не зависят: по ним облако узнаёт устройство.
void test_device_fields_are_sent_anyway() {
    JsonDocument doc;
    build(reading(), settings(), doc);

    TEST_ASSERT_EQUAL_STRING("abcd1234", doc["key"]);
    TEST_ASSERT_EQUAL_STRING("12345678", doc["sn"]);
    TEST_ASSERT_EQUAL_STRING("0.2.0", doc["fw"]);
    TEST_ASSERT_EQUAL_STRING("192.168.1.50", doc["ip"]);
}

// --- выбранные показания ---------------------------------------------------

void test_total_goes_with_the_chosen_code() {
    Settings s = settings();
    s.totalType = core::DT_ELECTRICITY;
    JsonDocument doc;
    build(reading(), s, doc);

    TEST_ASSERT_EQUAL_DOUBLE(1000, doc["total"].as<double>());
    TEST_ASSERT_EQUAL(core::DT_ELECTRICITY, doc["data_type"].as<int>());
}

// Номер поля — номер тарифа в счётчике: T2 уходит как total2, даже когда T1
// пропущен. Перенумерация развязала бы облако со страницей статуса.
void test_tariff_keeps_its_number_when_a_neighbour_is_off() {
    Settings s = settings();
    s.tariffType[1] = core::DT_NIGHT;
    JsonDocument doc;
    build(reading(), s, doc);

    TEST_ASSERT_FALSE_MESSAGE(doc["total1"].is<double>(), "невыбранный тариф ушёл");
    TEST_ASSERT_FALSE_MESSAGE(doc["data_type1"].is<int>(), "код невыбранного тарифа ушёл");
    TEST_ASSERT_EQUAL_DOUBLE(200, doc["total2"].as<double>());
    TEST_ASSERT_EQUAL(core::DT_NIGHT, doc["data_type2"].as<int>());
}

// Код не привязан к номеру: ночным может быть назначен любой тариф.
void test_any_code_fits_any_tariff() {
    Settings s = settings();
    s.tariffType[0] = core::DT_NIGHT;
    s.tariffType[3] = core::DT_DAY;
    JsonDocument doc;
    build(reading(), s, doc);

    TEST_ASSERT_EQUAL(core::DT_NIGHT, doc["data_type1"].as<int>());
    TEST_ASSERT_EQUAL(core::DT_DAY, doc["data_type4"].as<int>());
}

// Выбор есть, а показания нет: счётчик вернул меньше тарифов, чем настроено.
void test_unread_tariff_is_not_sent() {
    MeterData m = reading();
    m.tariffCount = 2;
    Settings s = settings();
    s.tariffType[2] = core::DT_PEAK;
    JsonDocument doc;
    build(m, s, doc);

    TEST_ASSERT_FALSE_MESSAGE(doc["total3"].is<double>(), "непрочитанный тариф ушёл");
    TEST_ASSERT_FALSE_MESSAGE(doc["data_type3"].is<int>(), "код непрочитанного тарифа ушёл");
}

void test_all_tariffs_go_together_with_the_total() {
    Settings s = settings();
    s.totalType = core::DT_ELECTRICITY;
    s.tariffType[0] = core::DT_DAY;
    s.tariffType[1] = core::DT_NIGHT;
    s.tariffType[2] = core::DT_PEAK;
    s.tariffType[3] = core::DT_HALF_PEAK;
    JsonDocument doc;
    build(reading(), s, doc);

    TEST_ASSERT_EQUAL_DOUBLE(1000, doc["total"].as<double>());
    TEST_ASSERT_EQUAL_DOUBLE(100, doc["total1"].as<double>());
    TEST_ASSERT_EQUAL_DOUBLE(400, doc["total4"].as<double>());
    TEST_ASSERT_EQUAL(core::DT_HALF_PEAK, doc["data_type4"].as<int>());
}

// --- проверка кода --------------------------------------------------------

void test_only_known_codes_are_valid() {
    TEST_ASSERT_TRUE(core::validDataType(core::DT_NONE));
    TEST_ASSERT_TRUE(core::validDataType(core::DT_ELECTRICITY));
    TEST_ASSERT_TRUE(core::validDataType(core::DT_HALF_PEAK));
    // 0 и 1 — счётчики воды у Waterius, 3 и 4 — другие ресурсы, 9 нет совсем
    TEST_ASSERT_FALSE(core::validDataType(0));
    TEST_ASSERT_FALSE(core::validDataType(1));
    TEST_ASSERT_FALSE(core::validDataType(3));
    TEST_ASSERT_FALSE(core::validDataType(4));
    TEST_ASSERT_FALSE(core::validDataType(9));
    TEST_ASSERT_FALSE(core::validDataType(-2));
}

// --- буфер -----------------------------------------------------------------

void test_payload_that_does_not_fit_is_rejected() {
    char body[64];
    DeviceInfo dev;
    Settings s = settings();
    s.totalType = core::DT_ELECTRICITY;
    TEST_ASSERT_EQUAL_MESSAGE(0, buildCloudPayload(reading(), 0, s, dev, body, sizeof(body)),
                              "обрезанное тело выдано за готовое");
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_nothing_is_sent_until_the_user_picks_types);
    RUN_TEST(test_device_fields_are_sent_anyway);
    RUN_TEST(test_total_goes_with_the_chosen_code);
    RUN_TEST(test_tariff_keeps_its_number_when_a_neighbour_is_off);
    RUN_TEST(test_any_code_fits_any_tariff);
    RUN_TEST(test_unread_tariff_is_not_sent);
    RUN_TEST(test_all_tariffs_go_together_with_the_total);
    RUN_TEST(test_only_known_codes_are_valid);
    RUN_TEST(test_payload_that_does_not_fit_is_rejected);
    return UNITY_END();
}
