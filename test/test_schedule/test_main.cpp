// Юнит-тесты расписания отправки (src/core/schedule.h).
//
//   ~/.platformio/penv/bin/pio test -e native -f test_schedule
#include <unity.h>

#include "core/schedule.h"

using core::jitterMs;
using core::retryDelayMs;

static const uint32_t SEC = 1000;
static const uint32_t MIN = 60 * SEC;

void setUp() {}
void tearDown() {}

// --- разброс ---------------------------------------------------------------

void test_jitter_stays_inside_the_span() {
    for (uint32_t seed = 0; seed < 5000; seed += 7)
        TEST_ASSERT_TRUE_MESSAGE(jitterMs(seed, 5 * MIN) < 5 * MIN, "сдвиг вылез за отведённый разброс");
}

void test_jitter_is_stable_for_a_device() {
    TEST_ASSERT_EQUAL(jitterMs(0xC0FFEE, 5 * MIN), jitterMs(0xC0FFEE, 5 * MIN));
}

void test_jitter_spreads_neighbouring_chip_ids() {
    // Устройства одной партии имеют соседние chipId. Сдвиги должны
    // разойтись по всему разбросу, а не сесть кучей в его начале.
    const uint32_t base = 0x00A1B200;
    const uint32_t span = 5 * MIN;
    const int DEVICES = 64, BUCKETS = 8;
    int buckets[BUCKETS] = {0};
    uint32_t seen[DEVICES];

    for (int i = 0; i < DEVICES; ++i) {
        seen[i] = jitterMs(base + i, span);
        buckets[seen[i] * BUCKETS / span]++;
    }
    for (int i = 0; i < BUCKETS; ++i)
        TEST_ASSERT_TRUE_MESSAGE(buckets[i] > 0, "часть разброса не используется совсем");

    for (int i = 0; i < DEVICES; ++i)
        for (int j = i + 1; j < DEVICES; ++j)
            TEST_ASSERT_TRUE_MESSAGE(seen[i] != seen[j], "два устройства получили один сдвиг");
}

void test_zero_span_means_no_jitter() { TEST_ASSERT_EQUAL(0, jitterMs(12345, 0)); }

// --- рост паузы ------------------------------------------------------------

void test_retry_doubles_up_to_the_cap() {
    const uint32_t base = 5 * MIN, max = 60 * MIN;
    TEST_ASSERT_EQUAL(0, retryDelayMs(0, base, max));
    TEST_ASSERT_EQUAL(5 * MIN, retryDelayMs(1, base, max));
    TEST_ASSERT_EQUAL(10 * MIN, retryDelayMs(2, base, max));
    TEST_ASSERT_EQUAL(20 * MIN, retryDelayMs(3, base, max));
    TEST_ASSERT_EQUAL(40 * MIN, retryDelayMs(4, base, max));
    TEST_ASSERT_EQUAL(60 * MIN, retryDelayMs(5, base, max));
}

void test_retry_never_exceeds_the_cap() {
    // Сервер лежит сутки: пауза упирается в потолок и там остаётся
    for (uint8_t attempt = 1; attempt < 255; ++attempt)
        TEST_ASSERT_TRUE_MESSAGE(retryDelayMs(attempt, 5 * MIN, 60 * MIN) <= 60 * MIN,
                                 "пауза переросла потолок или переполнилась");
}

void test_retry_never_shrinks() {
    uint32_t prev = 0;
    for (uint8_t attempt = 1; attempt < 60; ++attempt) {
        uint32_t d = retryDelayMs(attempt, 5 * MIN, 60 * MIN);
        TEST_ASSERT_TRUE_MESSAGE(d >= prev, "пауза уменьшилась с ростом числа неудач");
        prev = d;
    }
}

void test_retry_with_huge_base_does_not_overflow() {
    uint32_t base = 0x40000000;  // около 12 суток
    for (uint8_t attempt = 1; attempt < 40; ++attempt)
        TEST_ASSERT_TRUE(retryDelayMs(attempt, base, 0xFFFFFFFFUL) >= base);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_jitter_stays_inside_the_span);
    RUN_TEST(test_jitter_is_stable_for_a_device);
    RUN_TEST(test_jitter_spreads_neighbouring_chip_ids);
    RUN_TEST(test_zero_span_means_no_jitter);
    RUN_TEST(test_retry_doubles_up_to_the_cap);
    RUN_TEST(test_retry_never_exceeds_the_cap);
    RUN_TEST(test_retry_never_shrinks);
    RUN_TEST(test_retry_with_huge_base_does_not_overflow);
    return UNITY_END();
}
