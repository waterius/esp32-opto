// Юнит-тесты причины перезагрузки (src/core/restart_reason.*).
//
// Смысл в приоритете источников: плановая причина знает больше аппаратной.
// esp_reset_reason() на любую нашу перезагрузку отвечает «программная», и по
// такому ответу невозможно отличить смену настроек от срабатывания сторожа.
//
//   ~/.platformio/penv/bin/pio test -e native -f test_restart_reason
#include <unity.h>

#include <string.h>

#include "core/restart_reason.h"

using core::plannedRestartText;
using core::RestartReason;
using core::restartText;

void setUp() {}
void tearDown() {}

void test_planned_reason_wins_over_hardware() {
    // Так выглядит перезагрузка по кнопке: NVS знает точнее, чем «программная»
    TEST_ASSERT_EQUAL_STRING("кнопка на странице",
                             restartText(RestartReason::WebButton, false, "программная перезагрузка"));
}

void test_planned_reason_wins_over_watchdog() {
    // Сторож мог оставить метку в прошлый раз и не быть очищенным — плановая всё равно точнее
    TEST_ASSERT_EQUAL_STRING("смена настроек",
                             restartText(RestartReason::Settings, true, "программная перезагрузка"));
}

void test_watchdog_wins_over_hardware() {
    TEST_ASSERT_EQUAL_STRING("сторож главного цикла",
                             restartText(RestartReason::Unknown, true, "программная перезагрузка"));
}

void test_hardware_is_the_fallback() {
    TEST_ASSERT_EQUAL_STRING("подано питание",
                             restartText(RestartReason::Unknown, false, "подано питание"));
    TEST_ASSERT_EQUAL_STRING("просадка питания",
                             restartText(RestartReason::Unknown, false, "просадка питания"));
}

void test_nothing_known_at_all() {
    TEST_ASSERT_EQUAL_STRING("неизвестна", restartText(RestartReason::Unknown, false, nullptr));
    TEST_ASSERT_EQUAL_STRING("неизвестна", restartText(RestartReason::Unknown, false, ""));
}

void test_every_planned_reason_has_text() {
    const RestartReason all[] = {RestartReason::Settings, RestartReason::WebButton,
                                 RestartReason::NoNetwork, RestartReason::OtaCloud,
                                 RestartReason::OtaWeb, RestartReason::FactoryReset};
    for (RestartReason r : all) {
        const char* text = plannedRestartText(r);
        TEST_ASSERT_NOT_NULL_MESSAGE(text, "плановая причина без текста");
        TEST_ASSERT_TRUE_MESSAGE(strlen(text) > 0, "пустой текст причины");
    }
    TEST_ASSERT_NULL(plannedRestartText(RestartReason::Unknown));
}

void test_unknown_value_from_a_future_firmware() {
    // В NVS может лежать код от прошивки новее этой — не должен ломать вывод
    RestartReason alien = (RestartReason)200;
    TEST_ASSERT_NULL(plannedRestartText(alien));
    TEST_ASSERT_EQUAL_STRING("подано питание", restartText(alien, false, "подано питание"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_planned_reason_wins_over_hardware);
    RUN_TEST(test_planned_reason_wins_over_watchdog);
    RUN_TEST(test_watchdog_wins_over_hardware);
    RUN_TEST(test_hardware_is_the_fallback);
    RUN_TEST(test_nothing_known_at_all);
    RUN_TEST(test_every_planned_reason_has_text);
    RUN_TEST(test_unknown_value_from_a_future_firmware);
    return UNITY_END();
}
