#include "watchdog.h"

#include <Arduino.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <soc/rtc.h>
#include <soc/rtc_cntl_reg.h>

#include <atomic>

#include "log.h"

namespace watchdog {
namespace {

// Бюджет цикла. Считаем с запасом: HTTPS-запрос — до 12 секунд, обмен со
// счётчиком с перебором адресов бывает и дольше, но он кормит сторож через
// core::sleepMs(), пока идёт.
const uint32_t LOOP_TIMEOUT_MS = 60000;
const uint64_t CHECK_PERIOD_US = 1000000;

// RTC WDT — последняя линия. Срок заведомо больше любой законной задержки,
// включая перезапуск радио и запись образа OTA между кормлениями.
const uint32_t RTC_TIMEOUT_MS = 120000;

// Функции rtc_wdt_* в arduino-esp32 объявлены в заголовке, но не собраны ни в
// одну библиотеку: реализация осталась в загрузчике. Поэтому работаем с
// регистрами напрямую — ровно то же, что делает rtc_wdt.c в ESP-IDF.
// Эти два значения одинаковы у всей линейки, но в заголовках S3 их нет.
#ifndef RTC_CNTL_WDT_WKEY_VALUE
#define RTC_CNTL_WDT_WKEY_VALUE 0x50D83AA1
#endif
#ifndef RTC_WDT_STG_SEL_RESET_SYSTEM
#define RTC_WDT_STG_SEL_RESET_SYSTEM 3
#endif

uint32_t rtcTimeoutS_ = 0;  // фактическая выдержка, посчитанная по частоте RTC

// Регистры сторожа защищены от случайной записи ключом.
inline void rtcUnlock() { WRITE_PERI_REG(RTC_CNTL_WDTWPROTECT_REG, RTC_CNTL_WDT_WKEY_VALUE); }
inline void rtcLock() { WRITE_PERI_REG(RTC_CNTL_WDTWPROTECT_REG, 0); }

const uint32_t MARK = 0xA5107D06;  // «сторож цикла сработал», произвольное число

// RTC_NOINIT переживает программную перезагрузку и не изнашивает флеш.
// При подаче питания там мусор, но совпадение с меткой практически исключено.
RTC_NOINIT_ATTR uint32_t rtcMark;

std::atomic<uint32_t> lastFeedMs{0};
bool tripped_ = false;
bool loopArmed_ = false;
bool rtcArmed_ = false;
esp_timer_handle_t timer = nullptr;

// Работает в задаче esp_timer. Log здесь звать нельзя: он берёт мьютекс,
// а зависший loop() мог остаться с этим мьютексом в руках.
void check(void*) {
    if (millis() - lastFeedMs.load() < LOOP_TIMEOUT_MS) return;
    rtcMark = MARK;
    ets_printf("\nwatchdog: loop() stalled for %lus, restarting\n",
               (unsigned long)(LOOP_TIMEOUT_MS / 1000));
    esp_restart();
}

void startRtcWatchdog() {
    uint32_t slowHz = rtc_clk_slow_freq_get_hz();
    if (!slowHz) return;  // частоту не узнать — выдержку не посчитать
    uint32_t ticks = (uint32_t)((uint64_t)slowHz * RTC_TIMEOUT_MS / 1000);

    rtcUnlock();
    REG_SET_BIT(RTC_CNTL_WDTFEED_REG, RTC_CNTL_WDT_FEED);  // счётчик с нуля
    REG_WRITE(RTC_CNTL_WDTCONFIG1_REG, ticks);             // выдержка ступени 0
    // Ступень 0 перезагружает систему; длительность сигнала сброса — максимум.
    // Запись всего слова заодно гасит FLASHBOOT_MOD_EN, иначе сторож считал бы
    // загрузку из флеша своим делом.
    REG_WRITE(RTC_CNTL_WDTCONFIG0_REG, RTC_CNTL_WDT_EN |
                                           (RTC_WDT_STG_SEL_RESET_SYSTEM << RTC_CNTL_WDT_STG0_S) |
                                           (7 << RTC_CNTL_WDT_SYS_RESET_LENGTH_S) |
                                           (7 << RTC_CNTL_WDT_CPU_RESET_LENGTH_S));
    // Читаем обратно: если сторож не взвёлся, об этом надо сказать вслух
    rtcArmed_ = (REG_READ(RTC_CNTL_WDTCONFIG0_REG) & RTC_CNTL_WDT_EN) != 0;
    rtcTimeoutS_ = REG_READ(RTC_CNTL_WDTCONFIG1_REG) / slowHz;
    rtcLock();
}

void rtcFeed() {
    rtcUnlock();
    REG_SET_BIT(RTC_CNTL_WDTFEED_REG, RTC_CNTL_WDT_FEED);
    rtcLock();
}

}  // namespace

void begin() {
    tripped_ = rtcMark == MARK;
    rtcMark = 0;
    lastFeedMs.store(millis());

    esp_timer_create_args_t args = {};
    args.callback = check;
    args.name = "loop_wdt";
    loopArmed_ = esp_timer_create(&args, &timer) == ESP_OK &&
                 esp_timer_start_periodic(timer, CHECK_PERIOD_US) == ESP_OK;

    startRtcWatchdog();

    // Молчать об этом нельзя: без сторожей устройство после зависания
    // не вернётся само, а понять это снаружи невозможно
    if (loopArmed_ && rtcArmed_) {
        Log.printf("Сторожа: цикл %lu с, RTC %lu с\n", (unsigned long)(LOOP_TIMEOUT_MS / 1000),
                   (unsigned long)rtcTimeoutS_);
    } else {
        Log.error("ВНИМАНИЕ: сторож цикла %s, сторож RTC %s\n",
                   loopArmed_ ? "запущен" : "НЕ ЗАПУСТИЛСЯ",
                   rtcArmed_ ? "запущен" : "НЕ ЗАПУСТИЛСЯ");
    }
}

void feed() {
    lastFeedMs.store(millis());
    if (rtcArmed_) rtcFeed();
}

bool trippedLastBoot() { return tripped_; }
bool loopWatchdogArmed() { return loopArmed_; }
bool rtcWatchdogArmed() { return rtcArmed_; }

const char* resetReason() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "подано питание";
        case ESP_RST_EXT: return "внешний сброс";
        case ESP_RST_SW: return "программная перезагрузка";
        case ESP_RST_PANIC: return "паника: исключение в прошивке";
        case ESP_RST_INT_WDT: return "сторож прерываний";
        case ESP_RST_TASK_WDT: return "сторож задач";
        case ESP_RST_WDT: return "аппаратный сторож RTC: система зависла";
        case ESP_RST_DEEPSLEEP: return "выход из глубокого сна";
        case ESP_RST_BROWNOUT: return "просадка питания";
        case ESP_RST_SDIO: return "сброс по SDIO";
        default: return "неизвестна";
    }
}

uint8_t resetReasonCode() { return (uint8_t)esp_reset_reason(); }

}  // namespace watchdog
