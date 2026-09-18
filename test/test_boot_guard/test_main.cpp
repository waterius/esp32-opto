// Юнит-тесты защиты от кирпича (src/core/boot_guard.h).
//
// Сценарий: облачный OTA залил прошивку, которая падает на старте. Без счётчика
// загрузок устройство уходит в вечный цикл перезагрузок, и вернуть его можно
// только паяльником.
//
//   ~/.platformio/penv/bin/pio test -e native -f test_boot_guard
#include <unity.h>

#include "core/boot_guard.h"

using core::BootGuard;

static const uint32_t SEC = 1000;
static const uint32_t MIN = 60 * SEC;

void setUp() {}
void tearDown() {}

// Модель NVS: счётчик переживает перезагрузку.
struct Flash {
    uint8_t bootCount = 0;
};

// Одна загрузка: uptimeMs — сколько прошивка продержалась до перезагрузки.
static bool boot(Flash& flash, uint32_t uptimeMs) {
    BootGuard guard;
    flash.bootCount = guard.onBoot(flash.bootCount);
    if (guard.takeBootIsGood(uptimeMs)) flash.bootCount = 0;
    return guard.safeMode();
}

void test_healthy_boot_clears_the_counter() {
    Flash flash;
    for (int i = 0; i < 20; ++i) {
        TEST_ASSERT_FALSE_MESSAGE(boot(flash, 10 * MIN), "исправная прошивка ушла в усечённый режим");
        TEST_ASSERT_EQUAL_MESSAGE(0, flash.bootCount, "счётчик не обнулён после удачной загрузки");
    }
}

void test_safe_mode_after_five_crashes() {
    Flash flash;
    for (int i = 0; i < 4; ++i)
        TEST_ASSERT_FALSE_MESSAGE(boot(flash, 3 * SEC), "усечённый режим включился слишком рано");
    TEST_ASSERT_TRUE_MESSAGE(boot(flash, 3 * SEC), "пятая неудачная загрузка не включила усечённый режим");
}

void test_safe_mode_persists_until_a_good_boot() {
    Flash flash;
    for (int i = 0; i < 5; ++i) boot(flash, 3 * SEC);
    TEST_ASSERT_TRUE(boot(flash, 3 * SEC));

    // Залили рабочую прошивку по /update — она доживает до пяти минут
    TEST_ASSERT_TRUE_MESSAGE(boot(flash, 6 * MIN), "эта загрузка ещё считается неудачной");
    TEST_ASSERT_EQUAL(0, flash.bootCount);
    TEST_ASSERT_FALSE_MESSAGE(boot(flash, 6 * MIN), "усечённый режим не выключился");
}

void test_boot_is_good_fires_once() {
    BootGuard guard;
    guard.onBoot(0);
    TEST_ASSERT_FALSE(guard.takeBootIsGood(1 * MIN));
    TEST_ASSERT_TRUE(guard.takeBootIsGood(5 * MIN));
    TEST_ASSERT_FALSE_MESSAGE(guard.takeBootIsGood(9 * MIN), "лишняя запись в NVS каждую итерацию loop()");
}

void test_counter_does_not_overflow() {
    Flash flash;
    flash.bootCount = 255;
    boot(flash, 1 * SEC);
    TEST_ASSERT_EQUAL_MESSAGE(255, flash.bootCount, "счётчик перевернулся и снял усечённый режим");
}

void test_safe_mode_can_be_disabled() {
    core::BootGuardCfg cfg;
    cfg.safeAfter = 0;
    Flash flash;
    for (int i = 0; i < 50; ++i) {
        BootGuard guard;
        guard.configure(cfg);
        flash.bootCount = guard.onBoot(flash.bootCount);
        TEST_ASSERT_FALSE(guard.safeMode());
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_healthy_boot_clears_the_counter);
    RUN_TEST(test_safe_mode_after_five_crashes);
    RUN_TEST(test_safe_mode_persists_until_a_good_boot);
    RUN_TEST(test_boot_is_good_fires_once);
    RUN_TEST(test_counter_does_not_overflow);
    RUN_TEST(test_safe_mode_can_be_disabled);
    return UNITY_END();
}
