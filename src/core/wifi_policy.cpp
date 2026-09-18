#include "wifi_policy.h"

namespace core {

void WifiPolicy::reset(uint32_t nowMs) {
    linkUp_ = false;
    lostSinceMs_ = nowMs;
    attemptActive_ = false;
    attemptStartMs_ = nowMs;
    attemptWasFast_ = false;
    retryStartMs_ = nowMs;
    retryDelayMs_ = 0;
    fails_ = 0;
    fastFails_ = 0;
    fastDisabled_ = false;
    forgetFastPending_ = false;
    restartPending_ = false;
}

uint32_t WifiPolicy::offlineMs(uint32_t nowMs) const { return linkUp_ ? 0 : nowMs - lostSinceMs_; }

bool WifiPolicy::takeForgetFastConnect() {
    bool pending = forgetFastPending_;
    forgetFastPending_ = false;  // сказали один раз; сам запрет держится до успеха
    return pending;
}

WifiAction WifiPolicy::step(const WifiFacts& facts, uint32_t nowMs) {
    if (facts.linkUp) {
        if (!linkUp_) {  // связь появилась: лестница начинается заново
            linkUp_ = true;
            attemptActive_ = false;
            restartPending_ = false;
            fails_ = 0;
            fastFails_ = 0;
            fastDisabled_ = false;
        }
        // Точку доступа гасим, только когда на ней никого нет
        if (facts.apActive && !facts.apBusy) return WifiAction::StopAp;
        return WifiAction::None;
    }

    if (linkUp_) {  // связь только что пропала
        linkUp_ = false;
        lostSinceMs_ = nowMs;
        retryStartMs_ = nowMs;
        retryDelayMs_ = 0;
    }

    // Попытка не уложилась в таймаут — это неудача, ступень лестницы вверх
    if (attemptActive_ && nowMs - attemptStartMs_ >= cfg_.connectTimeoutMs) {
        attemptActive_ = false;
        if (fails_ < 255) ++fails_;
        if (attemptWasFast_) {
            if (fastFails_ < 255) ++fastFails_;
            // Сохранённые канал и BSSID больше не находят сеть: дальше только скан
            if (cfg_.forgetFastAfter && fastFails_ >= cfg_.forgetFastAfter && !fastDisabled_) {
                fastDisabled_ = true;
                forgetFastPending_ = true;
            }
        }
        if (cfg_.restartRadioEvery && fails_ % cfg_.restartRadioEvery == 0) restartPending_ = true;
        retryStartMs_ = nowMs;
        retryDelayMs_ = facts.apActive ? cfg_.apRetryMs : 0;
    }

    // Сеть не настроена: только точка доступа. Ни попыток, ни перезагрузки —
    // перезагружаться тут бессмысленно, подключаться всё равно некуда.
    if (!facts.haveSsid) return facts.apActive ? WifiAction::None : WifiAction::StartAp;

    // Последняя ступень: стек залип так, что не помогает ничего. Пока кто-то
    // сидит на точке доступа и настраивает устройство, перезагрузка запрещена —
    // иначе оборвём ему сессию ровно в тот момент, когда он чинит настройки.
    if (cfg_.rebootAfterMs && !facts.apBusy && nowMs - lostSinceMs_ >= cfg_.rebootAfterMs)
        return WifiAction::Reboot;

    // Отказ по свежевведённой сети виден за секунду — поднимаем точку сразу,
    // иначе вернуться на страницу /wifi будет неоткуда целых две минуты.
    if (!facts.apActive && (facts.refusedNewNetwork || nowMs - lostSinceMs_ >= cfg_.apAfterMs))
        return WifiAction::StartAp;

    if (attemptActive_) return WifiAction::None;
    if (nowMs - retryStartMs_ < retryDelayMs_) return WifiAction::None;

    // Перезапуск радио уронит и точку доступа: пока на ней кто-то сидит, эту
    // ступень пропускаем (ESPHome так же не трогает адаптер при живом портале).
    if (restartPending_ && !facts.apBusy) {
        restartPending_ = false;
        retryStartMs_ = nowMs;
        retryDelayMs_ = 0;  // подключаемся сразу после перезапуска радио
        return WifiAction::RestartRadio;
    }
    return startAttempt(facts, nowMs);
}

// Быстрый коннект и полный скан чередуются: сохранённая пара может быть
// устаревшей, но и скан не всегда находит сеть с первого раза (ESPHome по той
// же причине держит фазы INITIAL_CONNECT и SCAN_CONNECTING рядом).
WifiAction WifiPolicy::startAttempt(const WifiFacts& facts, uint32_t nowMs) {
    attemptWasFast_ = facts.haveFastConnect && !fastDisabled_ && (fails_ % 2 == 0);
    attemptActive_ = true;
    attemptStartMs_ = nowMs;
    return attemptWasFast_ ? WifiAction::ConnectFast : WifiAction::ConnectScan;
}

}  // namespace core
