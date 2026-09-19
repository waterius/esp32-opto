// Модель железа и эфира для тестов политики Wi-Fi: виртуальные часы, роутер,
// который можно выключить или увести на другой канал, и точка доступа.
// Политика решает — модель исполняет, как это делает port/net.cpp.
#pragma once
#include <stdint.h>

#include <vector>

#include "core/wifi_policy.h"

class FakeWifi {
   public:
    // Условия эфира — их меняет тест
    bool routerUp = false;          // сеть в эфире
    bool routerMatchesFast = true;  // сохранённые канал и BSSID ещё те самые
    bool haveSsid = true;
    bool haveFast = true;
    bool apBusy = false;  // кто-то подключился к точке доступа
    // Когда человек последний раз что-то делал на страницах портала. Опрос
    // состояния сюда не входит: открытая вкладка не должна держать портал.
    uint32_t portalFedMs = 0;
    bool refusedNew = false;  // роутер отверг только что введённую сеть

    // Наблюдаемое состояние
    bool linkUp = false;
    bool apActive = false;
    bool forgotFastConnect = false;
    uint32_t now = 0;

    core::WifiPolicy policy;

    // Роутер отвечает на ассоциацию не мгновенно, но быстрее таймаута попытки
    static const uint32_t ASSOC_MS = 3000;

    void start(uint32_t startMs = 0) {
        now = startMs;
        policy.reset(now);
    }

    void tick(uint32_t stepMs = 100) {
        if (linkUp && !routerUp) linkUp = false;  // роутер пропал из эфира
        if (connecting_ && now - connectStartMs_ >= ASSOC_MS) {
            connecting_ = false;
            if (connectOk_) linkUp = true;
        }

        core::WifiFacts facts;
        facts.linkUp = linkUp;
        facts.haveSsid = haveSsid;
        facts.haveFastConnect = haveFast;
        facts.apActive = apActive;
        facts.apBusy = apActive && apBusy;
        facts.portalIdleMs = now - portalFedMs;
        facts.refusedNewNetwork = refusedNew;
        apply(policy.step(facts, now));

        if (policy.takeForgetFastConnect()) {
            forgotFastConnect = true;
            haveFast = false;  // порт стирает пару из настроек
        }
        now += stepMs;
    }

    void run(uint32_t durationMs, uint32_t stepMs = 100) {
        for (uint32_t passed = 0; passed < durationMs; passed += stepMs) tick(stepMs);
    }

    int count(core::WifiAction action) const {
        int n = 0;
        for (core::WifiAction a : log) n += (a == action);
        return n;
    }

    std::vector<core::WifiAction> log;  // все действия, кроме None

   private:
    void apply(core::WifiAction action) {
        if (action != core::WifiAction::None) log.push_back(action);
        switch (action) {
            case core::WifiAction::ConnectFast:
                connecting_ = true;
                connectStartMs_ = now;
                // Быстрый коннект бьёт в конкретные канал и BSSID: если роутер
                // переехал, попытка не найдёт сеть, сколько ни жди
                connectOk_ = routerUp && routerMatchesFast;
                break;
            case core::WifiAction::ConnectScan:
                connecting_ = true;
                connectStartMs_ = now;
                connectOk_ = routerUp;
                break;
            case core::WifiAction::RestartRadio:
                connecting_ = false;
                linkUp = false;
                break;
            case core::WifiAction::StartAp: apActive = true; break;
            case core::WifiAction::StopAp:
                apActive = false;
                apBusy = false;
                break;
            case core::WifiAction::Reboot: break;  // тест считает по журналу
            case core::WifiAction::None: break;
        }
    }

    bool connecting_ = false;
    bool connectOk_ = false;
    uint32_t connectStartMs_ = 0;
};
