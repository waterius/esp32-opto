// Юнит-тесты сторожа прозрачной сессии (src/core/session_guard.h).
//
// Сценарий, ради которого он появился: клиент RFC 2217 исчез, не закрыв сокет
// (ноутбук уснул, отвалился Wi-Fi, NAT забыл трансляцию). Опрос счётчика и
// отправка в облако вставали навсегда, а страница статуса бодро показывала
// «идёт прозрачная сессия».
//
//   ~/.platformio/penv/bin/pio test -e native -f test_session_guard
#include <unity.h>

#include "core/session_guard.h"

using core::SessionGuard;
using core::SessionVerdict;

static const uint32_t SEC = 1000;
static const uint32_t MIN = 60 * SEC;

void setUp() {}
void tearDown() {}

void test_closed_guard_never_fires() {
    SessionGuard g;
    TEST_ASSERT_FALSE(g.open());
    TEST_ASSERT_EQUAL(SessionVerdict::Keep, g.check(100 * MIN, true));
    TEST_ASSERT_EQUAL(SessionVerdict::Keep, g.check(100 * MIN, false));
}

void test_live_session_is_kept() {
    SessionGuard g;
    g.onOpen(0);
    for (uint32_t t = 0; t < 60 * MIN; t += 30 * SEC) {
        g.onTraffic(t);  // клиент шлёт байты — сессия рабочая
        TEST_ASSERT_EQUAL_MESSAGE(SessionVerdict::Keep, g.check(t, true), "живую сессию закрыли");
    }
}

void test_half_open_session_is_closed_by_idle_timeout() {
    SessionGuard g;
    g.onOpen(0);
    g.onTraffic(1 * MIN);  // последние байты — и клиент исчез

    TEST_ASSERT_EQUAL(SessionVerdict::Keep, g.check(10 * MIN, true));
    TEST_ASSERT_EQUAL_MESSAGE(SessionVerdict::Idle, g.check(11 * MIN + SEC, true),
                              "молчащая сессия держит опрос вечно");
}

void test_traffic_resets_the_idle_timer() {
    SessionGuard g;
    g.onOpen(0);
    g.onTraffic(9 * MIN);
    TEST_ASSERT_EQUAL(SessionVerdict::Keep, g.check(18 * MIN, true));
    g.onTraffic(18 * MIN);
    TEST_ASSERT_EQUAL(SessionVerdict::Keep, g.check(27 * MIN, true));
    TEST_ASSERT_EQUAL(SessionVerdict::Idle, g.check(29 * MIN, true));
}

void test_link_loss_closes_session_without_waiting_for_idle() {
    SessionGuard g;
    g.onOpen(0);
    g.onTraffic(10 * SEC);
    // Сети нет — сокет клиента мёртв, ждать десять минут незачем
    TEST_ASSERT_EQUAL(SessionVerdict::Keep, g.check(11 * SEC, false));  // ещё терпим
    TEST_ASSERT_EQUAL(SessionVerdict::LinkDown, g.check(20 * SEC, false));
}

// Признак «связь есть» бывает и ложным (espressif/arduino-esp32#12714), а
// рабочую сессию из-за одной итерации loop() рвать нельзя.
void test_short_link_flicker_keeps_the_session() {
    SessionGuard g;
    g.onOpen(0);
    for (uint32_t t = 0; t < 5 * MIN; t += SEC) {
        g.onTraffic(t);
        bool linkUp = (t / SEC) % 7 != 0;  // раз в семь секунд связь «пропадает»
        TEST_ASSERT_EQUAL_MESSAGE(SessionVerdict::Keep, g.check(t, linkUp),
                                  "сессию закрыли из-за мигания признака связи");
    }
}

void test_link_returns_before_the_grace_runs_out() {
    SessionGuard g;
    g.onOpen(0);
    g.onTraffic(0);
    TEST_ASSERT_EQUAL(SessionVerdict::Keep, g.check(2 * SEC, false));
    TEST_ASSERT_EQUAL(SessionVerdict::Keep, g.check(3 * SEC, true));  // связь вернулась
    // Отсчёт должен начаться заново, а не продолжиться с прошлого раза
    TEST_ASSERT_EQUAL(SessionVerdict::Keep, g.check(6 * SEC, false));
    TEST_ASSERT_EQUAL(SessionVerdict::LinkDown, g.check(12 * SEC, false));
}

void test_idle_timeout_can_be_disabled() {
    SessionGuard g;
    core::SessionGuardCfg cfg;
    cfg.idleMs = 0;
    g.configure(cfg);
    g.onOpen(0);
    TEST_ASSERT_EQUAL(SessionVerdict::Keep, g.check(10 * 60 * MIN, true));
}

void test_idle_is_measured_across_millis_rollover() {
    SessionGuard g;
    uint32_t start = 0xFFFFFFFFUL - 30 * SEC;
    g.onOpen(start);
    g.onTraffic(start);

    uint32_t later = start + 11 * MIN;  // переполнение по дороге
    TEST_ASSERT_UINT32_WITHIN(SEC, 11 * MIN, g.idleMs(later));
    TEST_ASSERT_EQUAL_MESSAGE(SessionVerdict::Idle, g.check(later, true),
                              "после переполнения millis сторож ослеп");
}

void test_reopen_starts_the_timer_over() {
    SessionGuard g;
    g.onOpen(0);
    g.onTraffic(0);
    TEST_ASSERT_EQUAL(SessionVerdict::Idle, g.check(20 * MIN, true));
    g.onClose();
    g.onOpen(20 * MIN);  // подключился новый клиент
    TEST_ASSERT_EQUAL(SessionVerdict::Keep, g.check(25 * MIN, true));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_closed_guard_never_fires);
    RUN_TEST(test_live_session_is_kept);
    RUN_TEST(test_half_open_session_is_closed_by_idle_timeout);
    RUN_TEST(test_traffic_resets_the_idle_timer);
    RUN_TEST(test_link_loss_closes_session_without_waiting_for_idle);
    RUN_TEST(test_short_link_flicker_keeps_the_session);
    RUN_TEST(test_link_returns_before_the_grace_runs_out);
    RUN_TEST(test_idle_timeout_can_be_disabled);
    RUN_TEST(test_idle_is_measured_across_millis_rollover);
    RUN_TEST(test_reopen_starts_the_timer_over);
    return UNITY_END();
}
