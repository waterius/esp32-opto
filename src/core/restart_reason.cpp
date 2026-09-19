#include "restart_reason.h"

namespace core {

const char* plannedRestartText(RestartReason reason) {
    switch (reason) {
        case RestartReason::Settings: return "смена настроек";
        case RestartReason::WebButton: return "кнопка на странице";
        case RestartReason::NoNetwork: return "не было сети";
        case RestartReason::OtaCloud: return "обновление из облака";
        case RestartReason::OtaWeb: return "обновление со страницы";
        case RestartReason::FactoryReset: return "сброс к заводским";
        case RestartReason::Unknown: return nullptr;
    }
    return nullptr;  // значение из будущей версии прошивки
}

const char* restartText(RestartReason planned, bool watchdogTripped, const char* hardware) {
    if (const char* text = plannedRestartText(planned)) return text;
    if (watchdogTripped) return "сторож главного цикла";
    return hardware && hardware[0] ? hardware : "неизвестна";
}

}  // namespace core
