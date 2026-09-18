#include "watchdog.h"

#include <Arduino.h>
#include <esp_system.h>
#include <esp_timer.h>

#include <atomic>

namespace watchdog {
namespace {

// Бюджет цикла. Считаем с запасом: HTTPS-запрос — до 12 секунд, обмен со
// счётчиком с перебором адресов бывает и дольше, но он кормит сторож через
// core::sleepMs(), пока идёт.
const uint32_t LOOP_TIMEOUT_MS = 60000;
const uint64_t CHECK_PERIOD_US = 1000000;
const uint32_t MARK = 0xA5107D06;  // «сторож сработал», произвольное число

// RTC_NOINIT переживает программную перезагрузку и не изнашивает флеш.
// При подаче питания там мусор, но совпадение с меткой практически исключено.
RTC_NOINIT_ATTR uint32_t rtcMark;

std::atomic<uint32_t> lastFeedMs{0};
bool tripped_ = false;
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

}  // namespace

void begin() {
    tripped_ = rtcMark == MARK;
    rtcMark = 0;
    lastFeedMs.store(millis());

    esp_timer_create_args_t args = {};
    args.callback = check;
    args.name = "loop_wdt";
    if (esp_timer_create(&args, &timer) != ESP_OK) return;
    esp_timer_start_periodic(timer, CHECK_PERIOD_US);
}

void feed() { lastFeedMs.store(millis()); }

bool trippedLastBoot() { return tripped_; }

const char* resetReason() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "подано питание";
        case ESP_RST_EXT: return "внешний сброс";
        case ESP_RST_SW: return "программная перезагрузка";
        case ESP_RST_PANIC: return "паника: исключение в прошивке";
        case ESP_RST_INT_WDT: return "сторож прерываний";
        case ESP_RST_TASK_WDT: return "сторож задач";
        case ESP_RST_WDT: return "сторож";
        case ESP_RST_DEEPSLEEP: return "выход из глубокого сна";
        case ESP_RST_BROWNOUT: return "просадка питания";
        case ESP_RST_SDIO: return "сброс по SDIO";
        default: return "неизвестна";
    }
}

uint8_t resetReasonCode() { return (uint8_t)esp_reset_reason(); }

}  // namespace watchdog
