// Ядро: политика восстановления Wi-Fi. По фактам от порта и часам решает, что
// делать дальше; про WiFi.h и Arduino не знает ничего, поэтому проверяется
// юнит-тестами на компьютере (test/test_wifi_policy).
//
// Зачем отдельный модуль: надеяться на WiFi.setAutoReconnect() нельзя — это
// самая частая причина «устройство молчит до ручного ресета». Лестница
// восстановления взята у ESPHome (wifi_component.cpp) и Tasmota
// (support_wifi.ino) и состоит из одних и тех же ступеней:
//
//   быстрый коннект (канал+BSSID) → полный скан → перезапуск радио →
//   точка доступа → перезагрузка.
//
// Последняя ступень у ESPHome называется reboot_timeout и по умолчанию 15
// минут; в документации прямо сказано, почему она есть: «the low level IP
// stack currently seems to have issues with WiFi where a full reboot is
// required to get the interface back working».
#pragma once
#include <stdint.h>

namespace core {

enum class WifiAction : uint8_t {
    None,
    ConnectFast,   // WiFi.begin(ssid, pass, channel, bssid) — без скана
    ConnectScan,   // WiFi.begin(ssid, pass) — полный скан
    RestartRadio,  // выключить и включить радио: стек залип
    StartAp,
    StopAp,
    Reboot,
};

struct WifiPolicyCfg {
    uint32_t connectTimeoutMs = 10000;  // после этого попытка считается неудачной
    uint32_t retryPauseMs = 5000;       // пауза между попытками, пока точки доступа нет
    uint8_t refusedAttempts = 3;        // столько отказов по новой сети — поднять точку; 0 — сразу
    uint32_t apAfterMs = 120000;        // нет сети столько — поднять точку доступа
    uint32_t apRetryMs = 60000;         // пауза между попытками при поднятой AP
    uint8_t restartRadioEvery = 4;      // неудач подряд до перезапуска радио; 0 — никогда
    uint8_t forgetFastAfter = 2;        // неудачных быстрых коннектов до отказа от пары
    uint32_t rebootAfterMs = 3600000;   // нет сети столько — перезагрузка; 0 — никогда
    // Пока на точке доступа кто-то настраивает устройство, попытки подключения
    // не делаются вовсе: радио одно, и каждая попытка уводит его в полный скан,
    // обрывая сессию. Подключение в это время — только по явной команде со
    // страницы (net::reconnect). Так же устроен портал waterius: в его цикле
    // нет ни одной самостоятельной попытки, только флаг от кнопки.
    //
    // От забытого на точке телефона защищает не лимит попыток, а простой:
    // столько портал живёт без действий человека, после чего лестница
    // возобновляется. У waterius это PORTAL_WATCHDOG_MS = 10 минут, и окно
    // продлевают действия, а не опрос страницы состояния. 0 — не сдаваться.
    uint32_t portalIdleGiveUpMs = 600000;
};

// Факты, которые порт сообщает политике на каждой итерации loop().
struct WifiFacts {
    bool linkUp = false;           // есть связь с роутером и адрес
    bool haveSsid = false;         // сеть задана в настройках
    bool haveFastConnect = false;  // сохранены канал и BSSID
    bool apActive = false;         // точка доступа поднята
    bool apBusy = false;           // к точке доступа кто-то подключён
    uint32_t portalIdleMs = 0;     // сколько прошло с последнего действия человека на страницах
    // Роутер ответил отказом на сеть, которая ещё ни разу не подключалась
    // (неверный пароль, сети нет в эфире). Это ответ, а не молчание: ждать
    // Роутер отверг сеть, которая ещё ни разу не подключалась. Ждать все
    // apAfterMs незачем — пользователю нужна страница /wifi. Но и по первому
    // отказу сдаваться нельзя: причина 15 (таймаут рукопожатия) приходит и при
    // неверном пароле, и когда кадры теряются на слабом сигнале. Поэтому точка
    // поднимается после refusedAttempts неудач подряд, а не после первой.
    bool refusedNewNetwork = false;
};

class WifiPolicy {
   public:
    void configure(const WifiPolicyCfg& cfg) { cfg_ = cfg; }
    const WifiPolicyCfg& config() const { return cfg_; }

    // Старт прошивки: связи нет, отсчёт простоя пошёл с nowMs.
    void reset(uint32_t nowMs);

    // Одно решение за вызов. Вызывается из loop() на каждой итерации.
    WifiAction step(const WifiFacts& facts, uint32_t nowMs);

    // Сохранённая пара канал+BSSID больше не годится (роутер переехал на другой
    // канал, сменили роутер, mesh увёл на другую точку): порт обязан её забыть,
    // иначе быстрый коннект будет промахиваться вечно. Флаг снимается одним
    // вызовом — как net::takeFastConnect().
    bool takeForgetFastConnect();

    // Сколько миллисекунд нет связи. При живой связи — 0.
    uint32_t offlineMs(uint32_t nowMs) const;

    uint8_t failures() const { return fails_; }

   private:
    // Запустить попытку подключения, вернув соответствующее действие.
    WifiAction startAttempt(const WifiFacts& facts, uint32_t nowMs);

    WifiPolicyCfg cfg_;

    bool linkUp_ = false;
    uint32_t lostSinceMs_ = 0;       // когда пропала связь
    bool attemptActive_ = false;     // идёт попытка подключения
    uint32_t attemptStartMs_ = 0;
    bool attemptWasFast_ = false;    // текущая попытка — быстрая
    uint32_t retryStartMs_ = 0;      // отсчёт паузы перед следующей попыткой
    uint32_t retryDelayMs_ = 0;      // длина этой паузы
    uint8_t fails_ = 0;              // неудачных попыток подряд
    uint8_t fastFails_ = 0;          // неудачных быстрых попыток подряд
    bool fastDisabled_ = false;      // до следующего успеха быстрый коннект не пробуем
    bool forgetFastPending_ = false; // порту ещё не сказали забыть пару
    bool restartPending_ = false;    // следующим шагом перезапустить радио

};

}  // namespace core
