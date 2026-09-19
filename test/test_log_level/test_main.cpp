// Юнит-тесты выбора приёмника по уровню (src/core/log_level.h).
//
// Смысл правила: в USB-serial идёт всё, а 16-килобайтный буфер страницы лога
// бережём — иначе к моменту разбора отказа из него вытеснится всё интересное.
// Тесты стерегут обе стороны: и что trace не просачивается на страницу при
// умолчаниях, и что поднятый порог буфера его туда пускает.
//
//   ~/.platformio/penv/bin/pio test -e native -f test_log_level
#include <unity.h>

#include "core/log_level.h"

using core::LogLevel;
using core::LogLevels;
using core::LogRoute;

void setUp() {}
void tearDown() {}

static const LogLevel ALL[] = {LogLevel::Error, LogLevel::Warn, LogLevel::Info, LogLevel::Debug,
                               LogLevel::Trace};
static const int COUNT = 5;

void test_defaults_keep_trace_out_of_the_page() {
    LogLevels levels;  // serial = trace, буфер = debug
    TEST_ASSERT_EQUAL_MESSAGE(LogRoute::SerialOnly, core::route(LogLevel::Trace, levels),
                              "trace просочился в буфер страницы");
    TEST_ASSERT_EQUAL_MESSAGE(LogRoute::Both, core::route(LogLevel::Debug, levels),
                              "байты оптопорта не попали на страницу лога");
    TEST_ASSERT_EQUAL(LogRoute::Both, core::route(LogLevel::Info, levels));
    TEST_ASSERT_EQUAL(LogRoute::Both, core::route(LogLevel::Warn, levels));
    TEST_ASSERT_EQUAL(LogRoute::Both, core::route(LogLevel::Error, levels));
}

void test_raised_buffer_threshold_lets_trace_through() {
    LogLevels levels;
    levels.buffer = LogLevel::Trace;
    TEST_ASSERT_EQUAL_MESSAGE(LogRoute::Both, core::route(LogLevel::Trace, levels),
                              "порог буфера подняли, а trace всё равно не доходит");
}

void test_quiet_serial_still_feeds_the_page() {
    LogLevels levels;
    levels.serial = LogLevel::Warn;
    levels.buffer = LogLevel::Debug;
    TEST_ASSERT_EQUAL_MESSAGE(LogRoute::BufferOnly, core::route(LogLevel::Info, levels),
                              "приглушили порт — потеряли строку и на странице");
    TEST_ASSERT_EQUAL(LogRoute::Both, core::route(LogLevel::Warn, levels));
}

void test_message_below_both_thresholds_is_dropped() {
    LogLevels levels;
    levels.serial = LogLevel::Error;
    levels.buffer = LogLevel::Error;
    TEST_ASSERT_EQUAL_MESSAGE(LogRoute::None, core::route(LogLevel::Debug, levels),
                              "сообщение ниже обоих порогов кому-то досталось");
    TEST_ASSERT_EQUAL(LogRoute::None, core::route(LogLevel::Warn, levels));
    TEST_ASSERT_EQUAL(LogRoute::Both, core::route(LogLevel::Error, levels));
}

// Error проходит всегда: порога ниже него нет.
void test_errors_are_never_filtered_out() {
    for (int s = 0; s < COUNT; ++s) {
        for (int b = 0; b < COUNT; ++b) {
            LogLevels levels;
            levels.serial = ALL[s];
            levels.buffer = ALL[b];
            TEST_ASSERT_EQUAL_MESSAGE(LogRoute::Both, core::route(LogLevel::Error, levels),
                                      "ошибку отфильтровали");
        }
    }
}

// Все 25 комбинаций: приёмник берёт сообщение ровно тогда, когда оно не
// подробнее его порога, и пороги друг на друга не влияют.
void test_every_threshold_pair_routes_independently() {
    for (int s = 0; s < COUNT; ++s) {
        for (int b = 0; b < COUNT; ++b) {
            LogLevels levels;
            levels.serial = ALL[s];
            levels.buffer = ALL[b];
            for (int m = 0; m < COUNT; ++m) {
                LogRoute got = core::route(ALL[m], levels);
                bool serial = m <= s;
                bool buffer = m <= b;
                LogRoute want = serial && buffer ? LogRoute::Both
                                : serial         ? LogRoute::SerialOnly
                                : buffer         ? LogRoute::BufferOnly
                                                 : LogRoute::None;
                TEST_ASSERT_EQUAL_MESSAGE(want, got, "приёмники выбраны не по своим порогам");
            }
        }
    }
}

// Если уровень прошёл, то и любое более крупное событие проходит тоже.
void test_routing_is_monotonic() {
    for (int s = 0; s < COUNT; ++s) {
        for (int b = 0; b < COUNT; ++b) {
            LogLevels levels;
            levels.serial = ALL[s];
            levels.buffer = ALL[b];
            for (int m = 1; m < COUNT; ++m) {
                bool fine = core::accepts(levels.serial, ALL[m]);
                bool coarse = core::accepts(levels.serial, ALL[m - 1]);
                TEST_ASSERT_MESSAGE(!fine || coarse, "подробное прошло, а крупное — нет");
            }
        }
    }
}

void test_level_names_are_latin_and_distinct() {
    TEST_ASSERT_EQUAL_STRING("error", core::levelName(LogLevel::Error));
    TEST_ASSERT_EQUAL_STRING("warn", core::levelName(LogLevel::Warn));
    TEST_ASSERT_EQUAL_STRING("info", core::levelName(LogLevel::Info));
    TEST_ASSERT_EQUAL_STRING("debug", core::levelName(LogLevel::Debug));
    TEST_ASSERT_EQUAL_STRING("trace", core::levelName(LogLevel::Trace));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_keep_trace_out_of_the_page);
    RUN_TEST(test_raised_buffer_threshold_lets_trace_through);
    RUN_TEST(test_quiet_serial_still_feeds_the_page);
    RUN_TEST(test_message_below_both_thresholds_is_dropped);
    RUN_TEST(test_errors_are_never_filtered_out);
    RUN_TEST(test_every_threshold_pair_routes_independently);
    RUN_TEST(test_routing_is_monotonic);
    RUN_TEST(test_level_names_are_latin_and_distinct);
    return UNITY_END();
}
