// Юнит-тесты политики восстановления Wi-Fi (src/core/wifi_policy.*).
//
// Каждый тест — сценарий отказа, из-за которого прошивка до этих правок
// замолкала навсегда: устаревшая пара канал+BSSID, залипший стек, роутер,
// пропавший на час. Железа не нужно: часы и эфир — модель в fake_wifi.h.
//
//   ~/.platformio/penv/bin/pio test -e native -f test_wifi_policy
#include <unity.h>

#include "fake_wifi.h"

using core::WifiAction;

static const uint32_t SEC = 1000;
static const uint32_t MIN = 60 * SEC;

void setUp() {}
void tearDown() {}

// --- Нормальная работа -----------------------------------------------------

void test_connects_with_saved_pair_and_stays_connected() {
    FakeWifi w;
    w.routerUp = true;
    w.start();
    w.run(5 * MIN);

    TEST_ASSERT_TRUE_MESSAGE(w.linkUp, "не подключились к живому роутеру");
    TEST_ASSERT_EQUAL_MESSAGE(1, w.count(WifiAction::ConnectFast), "лишние попытки при живой связи");
    TEST_ASSERT_EQUAL_MESSAGE(0, w.count(WifiAction::ConnectScan), "скан при исправной паре не нужен");
    TEST_ASSERT_EQUAL_MESSAGE(0, w.count(WifiAction::StartAp), "точка доступа при живой связи");
    TEST_ASSERT_EQUAL_MESSAGE(0, w.count(WifiAction::Reboot), "перезагрузка при живой связи");
}

void test_full_scan_when_no_pair_saved() {
    FakeWifi w;
    w.routerUp = true;
    w.haveFast = false;
    w.start();
    w.run(2 * MIN);

    TEST_ASSERT_TRUE(w.linkUp);
    TEST_ASSERT_EQUAL(0, w.count(WifiAction::ConnectFast));
    TEST_ASSERT_EQUAL(1, w.count(WifiAction::ConnectScan));
}

// --- Устаревшая пара канал+BSSID (аудит, пункт 3) --------------------------

void test_stale_pair_falls_back_to_scan_within_a_minute() {
    FakeWifi w;
    w.routerUp = true;
    w.routerMatchesFast = false;  // роутер переехал на другой канал
    w.start();
    w.run(1 * MIN);

    TEST_ASSERT_TRUE_MESSAGE(w.linkUp, "не догадались сходить полным сканом");
    TEST_ASSERT_TRUE(w.count(WifiAction::ConnectFast) >= 1);
    TEST_ASSERT_TRUE(w.count(WifiAction::ConnectScan) >= 1);
    TEST_ASSERT_EQUAL_MESSAGE(0, w.count(WifiAction::StartAp), "до точки доступа дойти не должны");
}

void test_stale_pair_is_forgotten_after_two_failed_fast_attempts() {
    FakeWifi w;
    w.routerUp = false;  // сети нет вовсе: быстрый коннект промахивается подряд
    w.start();
    w.run(2 * MIN);

    TEST_ASSERT_TRUE_MESSAGE(w.forgotFastConnect, "пару не забыли — быстрый коннект промахивался бы вечно");
    TEST_ASSERT_FALSE(w.haveFast);
}

void test_forgotten_pair_is_reported_once() {
    FakeWifi w;
    w.routerUp = false;
    w.start();
    w.run(10 * MIN);
    // haveFast уже false, повторных запросов «забудь» быть не должно
    TEST_ASSERT_FALSE(w.policy.takeForgetFastConnect());
}

// --- Залипший стек: перезапуск радио (аудит, пункт 2) ----------------------

void test_radio_is_restarted_after_several_failures() {
    FakeWifi w;
    w.routerUp = false;
    w.start();
    w.run(2 * MIN);

    TEST_ASSERT_TRUE_MESSAGE(w.count(WifiAction::RestartRadio) >= 1, "радио ни разу не перезапустили");
}

void test_radio_restart_is_followed_by_a_connect_attempt() {
    FakeWifi w;
    w.routerUp = false;
    w.start();
    w.run(2 * MIN);

    int restartAt = -1;
    for (size_t i = 0; i < w.log.size(); ++i) {
        if (w.log[i] == WifiAction::RestartRadio) {
            restartAt = (int)i;
            break;
        }
    }
    TEST_ASSERT_TRUE(restartAt >= 0);
    TEST_ASSERT_TRUE_MESSAGE(restartAt + 1 < (int)w.log.size(), "после перезапуска радио никто не подключается");
    WifiAction next = w.log[restartAt + 1];
    TEST_ASSERT_TRUE(next == WifiAction::ConnectScan || next == WifiAction::ConnectFast);
}

// --- Точка доступа ---------------------------------------------------------

void test_ap_comes_up_after_two_minutes_without_router() {
    FakeWifi w;
    w.routerUp = false;
    w.start();

    w.run(110 * SEC);
    TEST_ASSERT_FALSE_MESSAGE(w.apActive, "точка доступа поднялась раньше срока");
    w.run(30 * SEC);
    TEST_ASSERT_TRUE_MESSAGE(w.apActive, "точка доступа не поднялась через две минуты");
}

// Пароль ввели неверно — точка доступа нужна раньше двух минут, иначе вернуться
// на страницу /wifi будет неоткуда. Но и по первому отказу сдаваться нельзя:
// причина 15 приходит и при неверном пароле, и когда кадры рукопожатия теряются
// на слабом сигнале. Три попытки — примерно 45 секунд.
void test_refused_network_gets_three_attempts_before_ap() {
    FakeWifi w;
    w.routerUp = false;
    w.refusedNew = true;  // ARDUINO_EVENT_WIFI_STA_DISCONNECTED с AUTH_FAIL
    w.start();

    w.run(20 * SEC);
    TEST_ASSERT_FALSE_MESSAGE(w.apActive, "сдались раньше трёх попыток");
    int early = w.count(WifiAction::ConnectFast) + w.count(WifiAction::ConnectScan);
    TEST_ASSERT_TRUE_MESSAGE(early >= 2, "вторая попытка не состоялась");

    w.run(40 * SEC);
    TEST_ASSERT_TRUE_MESSAGE(w.apActive, "точка доступа не поднялась после трёх отказов");
    TEST_ASSERT_TRUE_MESSAGE(w.now < 75 * SEC, "точка доступа поднялась слишком поздно");
    int tries = w.count(WifiAction::ConnectFast) + w.count(WifiAction::ConnectScan);
    TEST_ASSERT_TRUE_MESSAGE(tries >= 3, "до точки доступа сделали меньше трёх попыток");
}

// Отказ роутера держится, пока сеть не сменят, — и не должен превращаться в
// непрерывный перебор попыток: каждая занимает эфир и мешает точке доступа.
void test_refused_network_does_not_spin() {
    FakeWifi w;
    w.routerUp = false;
    w.refusedNew = true;  // пароль не подошёл, и это не меняется
    w.start();
    w.run(10 * MIN, SEC);

    int attempts = w.count(WifiAction::ConnectFast) + w.count(WifiAction::ConnectScan);
    TEST_ASSERT_TRUE_MESSAGE(attempts > 0, "перестали пробовать совсем");
    // До точки доступа три попытки по 10 с с паузой 5 с, дальше попытка (10 с)
    // плюс пауза при поднятой точке (60 с) — около 11 за десять минут
    TEST_ASSERT_TRUE_MESSAGE(attempts <= 16, "подключение повторяется слишком часто");
}

// А вот у сети, которая уже работала, разрыв — обычное дело (роутер
// перезагрузился), и точку раньше срока поднимать не надо.
void test_working_network_does_not_raise_ap_early() {
    FakeWifi w;
    w.routerUp = true;
    w.start();
    w.run(1 * MIN);
    TEST_ASSERT_TRUE(w.linkUp);

    w.routerUp = false;  // refusedNew остаётся false: сеть уже подключалась
    w.run(90 * SEC);

    TEST_ASSERT_FALSE_MESSAGE(w.apActive, "точка доступа поднялась раньше двух минут");
}

void test_ap_goes_down_when_router_returns() {
    FakeWifi w;
    w.routerUp = false;
    w.start();
    w.run(3 * MIN);
    TEST_ASSERT_TRUE(w.apActive);

    w.routerUp = true;
    w.run(3 * MIN);
    TEST_ASSERT_TRUE_MESSAGE(w.linkUp, "роутер вернулся, а мы не подключились");
    TEST_ASSERT_FALSE_MESSAGE(w.apActive, "точка доступа осталась висеть");
}

void test_ap_stays_up_while_a_client_is_connected() {
    FakeWifi w;
    w.routerUp = false;
    w.start();
    w.run(3 * MIN);
    TEST_ASSERT_TRUE(w.apActive);

    w.apBusy = true;  // кто-то открыл страницу настроек
    w.routerUp = true;
    w.run(5 * MIN);

    TEST_ASSERT_TRUE_MESSAGE(w.linkUp, "к роутеру не подключились");
    TEST_ASSERT_TRUE_MESSAGE(w.apActive, "точку доступа погасили под ногами у настраивающего");
}

void test_radio_is_not_restarted_under_a_configuring_client() {
    FakeWifi w;
    w.routerUp = false;
    w.start();
    w.run(3 * MIN);
    TEST_ASSERT_TRUE(w.apActive);
    int before = w.count(WifiAction::RestartRadio);

    w.apBusy = true;  // кто-то настраивает устройство через точку доступа
    w.run(30 * MIN, SEC);

    TEST_ASSERT_EQUAL_MESSAGE(before, w.count(WifiAction::RestartRadio),
                              "перезапуск радио уронил точку доступа под настраивающим");
}

void test_ap_only_when_no_network_configured() {
    FakeWifi w;
    w.haveSsid = false;
    w.start();
    w.run(2 * 60 * MIN);

    TEST_ASSERT_TRUE(w.apActive);
    TEST_ASSERT_EQUAL_MESSAGE(0, w.count(WifiAction::ConnectFast), "подключаться некуда");
    TEST_ASSERT_EQUAL_MESSAGE(0, w.count(WifiAction::ConnectScan), "подключаться некуда");
    TEST_ASSERT_EQUAL_MESSAGE(0, w.count(WifiAction::Reboot), "перезагрузка не вернёт ненастроенную сеть");
}

// --- Последняя ступень: перезагрузка (аудит, пункт 2) ----------------------

void test_reboot_after_an_hour_without_network() {
    FakeWifi w;
    w.routerUp = false;
    w.start();

    w.run(55 * MIN, SEC);
    TEST_ASSERT_EQUAL_MESSAGE(0, w.count(WifiAction::Reboot), "перезагрузились раньше часа");
    w.run(10 * MIN, SEC);
    TEST_ASSERT_TRUE_MESSAGE(w.count(WifiAction::Reboot) >= 1, "час без сети — а перезагрузки нет");
}

void test_no_reboot_while_someone_configures_over_ap() {
    FakeWifi w;
    w.routerUp = false;
    w.start();
    w.run(3 * MIN);
    w.apBusy = true;

    w.run(2 * 60 * MIN, SEC);
    TEST_ASSERT_EQUAL_MESSAGE(0, w.count(WifiAction::Reboot), "оборвали сессию настройки перезагрузкой");
}

void test_reboot_can_be_disabled() {
    FakeWifi w;
    core::WifiPolicyCfg cfg;
    cfg.rebootAfterMs = 0;
    w.policy.configure(cfg);
    w.routerUp = false;
    w.start();
    w.run(3 * 60 * MIN, SEC);

    TEST_ASSERT_EQUAL(0, w.count(WifiAction::Reboot));
}

void test_short_outage_does_not_reboot() {
    FakeWifi w;
    w.routerUp = true;
    w.start();
    w.run(1 * MIN);
    TEST_ASSERT_TRUE(w.linkUp);

    w.routerUp = false;  // роутер перезагружается
    w.run(90 * SEC);
    w.routerUp = true;
    w.run(3 * MIN);

    TEST_ASSERT_TRUE_MESSAGE(w.linkUp, "не вернулись после короткого пропадания роутера");
    TEST_ASSERT_EQUAL_MESSAGE(0, w.count(WifiAction::Reboot), "перезагрузка из-за короткого пропадания");
}

// --- Потеря связи и возврат ------------------------------------------------

void test_ladder_restarts_after_reconnect() {
    FakeWifi w;
    w.routerUp = false;
    w.start();
    w.run(5 * MIN);  // накопили неудачи, подняли AP
    TEST_ASSERT_TRUE(w.policy.failures() > 0);

    w.routerUp = true;
    w.run(3 * MIN);
    TEST_ASSERT_TRUE(w.linkUp);
    TEST_ASSERT_EQUAL_MESSAGE(0, w.policy.failures(), "счётчик неудач не сброшен после успеха");
    TEST_ASSERT_EQUAL_MESSAGE(0, w.policy.offlineMs(w.now), "простой считается при живой связи");
}

// --- Переполнение millis() -------------------------------------------------

void test_survives_millis_rollover() {
    FakeWifi w;
    w.routerUp = false;
    w.start(0xFFFFFFFFUL - 30 * SEC);  // до переполнения 30 секунд

    w.run(70 * MIN, SEC);

    TEST_ASSERT_TRUE_MESSAGE(w.apActive, "после переполнения millis точка доступа не поднялась");
    TEST_ASSERT_TRUE_MESSAGE(w.count(WifiAction::Reboot) >= 1, "после переполнения millis перезагрузки нет");
}

void test_offline_ms_counts_across_rollover() {
    FakeWifi w;
    w.routerUp = false;
    w.start(0xFFFFFFFFUL - 10 * SEC);
    w.run(60 * SEC, SEC);

    uint32_t offline = w.policy.offlineMs(w.now);
    TEST_ASSERT_UINT32_WITHIN_MESSAGE(2 * SEC, 60 * SEC, offline, "простой посчитан неверно через переполнение");
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_connects_with_saved_pair_and_stays_connected);
    RUN_TEST(test_full_scan_when_no_pair_saved);
    RUN_TEST(test_stale_pair_falls_back_to_scan_within_a_minute);
    RUN_TEST(test_stale_pair_is_forgotten_after_two_failed_fast_attempts);
    RUN_TEST(test_forgotten_pair_is_reported_once);
    RUN_TEST(test_radio_is_restarted_after_several_failures);
    RUN_TEST(test_radio_restart_is_followed_by_a_connect_attempt);
    RUN_TEST(test_ap_comes_up_after_two_minutes_without_router);
    RUN_TEST(test_refused_network_gets_three_attempts_before_ap);
    RUN_TEST(test_refused_network_does_not_spin);
    RUN_TEST(test_working_network_does_not_raise_ap_early);
    RUN_TEST(test_ap_goes_down_when_router_returns);
    RUN_TEST(test_ap_stays_up_while_a_client_is_connected);
    RUN_TEST(test_radio_is_not_restarted_under_a_configuring_client);
    RUN_TEST(test_ap_only_when_no_network_configured);
    RUN_TEST(test_reboot_after_an_hour_without_network);
    RUN_TEST(test_no_reboot_while_someone_configures_over_ap);
    RUN_TEST(test_reboot_can_be_disabled);
    RUN_TEST(test_short_outage_does_not_reboot);
    RUN_TEST(test_ladder_restarts_after_reconnect);
    RUN_TEST(test_survives_millis_rollover);
    RUN_TEST(test_offline_ms_counts_across_rollover);
    return UNITY_END();
}
