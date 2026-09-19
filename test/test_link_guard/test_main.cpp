// Юнит-тесты контроля живости сети (src/core/link_guard.h).
//
// Сценарий, ради которого он появился: драйвер уверяет, что связь есть, а
// пакеты не ходят. И обратный, не менее важный: в сети, где шлюз не отвечает
// на ICMP, сторож обязан молчать, а не гонять устройство по перезагрузкам.
//
//   ~/.platformio/penv/bin/pio test -e native -f test_link_guard
#include <unity.h>

#include "core/link_guard.h"

using core::LinkGuard;

static const uint32_t SEC = 1000;
static const uint32_t MIN = 60 * SEC;

void setUp() {}
void tearDown() {}

// Прогон: сеть работает или нет, время идёт по минуте.
static LinkGuard run(bool networkAlive, uint32_t duration, bool armFirst = true) {
    LinkGuard g;
    uint32_t t = 0;
    g.reset(t);
    if (armFirst) {  // одна удачная проверка в начале — сторож взведён
        t += 1 * MIN;
        g.probeOk(t);
    }
    while (t < duration) {
        t += 10 * SEC;
        if (!g.shouldProbe(t)) continue;
        if (networkAlive) g.probeOk(t);
        else g.probeFailed(t);
    }
    return g;
}

void test_quiet_link_is_probed() {
    LinkGuard g;
    g.reset(0);
    TEST_ASSERT_FALSE_MESSAGE(g.shouldProbe(30 * SEC), "проверяем слишком часто");
    TEST_ASSERT_TRUE_MESSAGE(g.shouldProbe(61 * SEC), "молчащую связь никто не проверил");
}

void test_activity_postpones_the_probe() {
    LinkGuard g;
    g.reset(0);
    g.activity(50 * SEC);  // облако ответило
    TEST_ASSERT_FALSE(g.shouldProbe(61 * SEC));
    TEST_ASSERT_TRUE(g.shouldProbe(2 * MIN));
}

void test_live_network_is_never_declared_dead() {
    LinkGuard g = run(true, 60 * MIN);
    TEST_ASSERT_FALSE_MESSAGE(g.dead(), "живую связь объявили мёртвой");
    TEST_ASSERT_EQUAL(0, g.failures());
}

void test_silent_network_is_declared_dead() {
    LinkGuard g = run(false, 10 * MIN);
    TEST_ASSERT_TRUE_MESSAGE(g.dead(), "связь молчит, а сторож этого не заметил");
}

void test_death_needs_several_failures_in_a_row() {
    LinkGuard g;
    g.reset(0);
    g.probeOk(1 * MIN);
    g.probeFailed(2 * MIN);
    TEST_ASSERT_FALSE_MESSAGE(g.dead(), "одной неудачной проверки мало");
    g.probeFailed(3 * MIN);
    TEST_ASSERT_FALSE(g.dead());
    g.probeFailed(4 * MIN);
    TEST_ASSERT_TRUE(g.dead());
}

// Главный предохранитель: там, где ICMP не ходит вовсе, сторож обязан молчать.
void test_network_without_icmp_never_triggers() {
    LinkGuard g = run(false, 6 * 60 * MIN, /*armFirst=*/false);
    TEST_ASSERT_FALSE_MESSAGE(g.armed(), "сторож взвёлся без единой удачной проверки");
    TEST_ASSERT_FALSE_MESSAGE(g.dead(), "устройство ушло бы в вечные перезагрузки");
    TEST_ASSERT_EQUAL_MESSAGE(0, g.failures(), "невзведённый сторож считает отказы");
}

void test_one_success_recovers_the_guard() {
    LinkGuard g;
    g.reset(0);
    g.probeOk(1 * MIN);
    for (int i = 2; i <= 4; ++i) g.probeFailed(i * MIN);
    TEST_ASSERT_TRUE(g.dead());
    g.probeOk(5 * MIN);
    TEST_ASSERT_FALSE_MESSAGE(g.dead(), "связь вернулась, а сторож всё ещё хоронит");
}

void test_cloud_exchange_counts_as_proof() {
    LinkGuard g;
    g.reset(0);
    g.probeOk(1 * MIN);
    g.probeFailed(2 * MIN);
    g.probeFailed(3 * MIN);
    g.activity(3 * MIN + SEC);  // облако ответило — значит сеть жива
    TEST_ASSERT_EQUAL_MESSAGE(0, g.failures(), "успешный обмен не сбросил счётчик");
    TEST_ASSERT_FALSE(g.dead());
}

void test_reconnect_clears_the_verdict() {
    LinkGuard g;
    g.reset(0);
    g.probeOk(1 * MIN);
    for (int i = 2; i <= 4; ++i) g.probeFailed(i * MIN);
    TEST_ASSERT_TRUE(g.dead());
    g.reset(5 * MIN);  // драйвер переподключился
    TEST_ASSERT_FALSE(g.dead());
}

void test_can_be_disabled() {
    LinkGuard g;
    core::LinkGuardCfg cfg;
    cfg.deadAfter = 0;
    g.configure(cfg);
    g.reset(0);
    g.probeOk(1 * MIN);
    for (int i = 2; i < 50; ++i) g.probeFailed(i * MIN);
    TEST_ASSERT_FALSE(g.dead());
}

void test_survives_millis_rollover() {
    LinkGuard g;
    uint32_t t = 0xFFFFFFFFUL - 30 * SEC;
    g.reset(t);
    g.probeOk(t);
    t += 10 * MIN;  // переполнение по дороге
    TEST_ASSERT_TRUE_MESSAGE(g.shouldProbe(t), "после переполнения millis проверки встали");
    for (int i = 0; i < 3; ++i) {
        g.probeFailed(t);
        t += 2 * MIN;
    }
    TEST_ASSERT_TRUE(g.dead());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_quiet_link_is_probed);
    RUN_TEST(test_activity_postpones_the_probe);
    RUN_TEST(test_live_network_is_never_declared_dead);
    RUN_TEST(test_silent_network_is_declared_dead);
    RUN_TEST(test_death_needs_several_failures_in_a_row);
    RUN_TEST(test_network_without_icmp_never_triggers);
    RUN_TEST(test_one_success_recovers_the_guard);
    RUN_TEST(test_cloud_exchange_counts_as_proof);
    RUN_TEST(test_reconnect_clears_the_verdict);
    RUN_TEST(test_can_be_disabled);
    RUN_TEST(test_survives_millis_rollover);
    return UNITY_END();
}
