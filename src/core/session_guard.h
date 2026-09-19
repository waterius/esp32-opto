// Ядро: сторож прозрачной сессии. Пока сессия открыта, опрос счётчика и
// отправка в облако стоят — значит, «открыта» не должно означать «навсегда».
//
// Зачем: recv() на сокете клиента блокирующий, а TCP про полуоткрытое
// соединение не узнаёт сам. Клиент, который уснул, потерял Wi-Fi или прошёл
// через NAT с истёкшей трансляцией, оставлял сессию открытой вечно — и
// устройство молча переставало ходить в облако до ручного ресета.
//
// Тот же приём, что portal_watchdog в waterius: считаем время без байт и
// закрываем сессию сами.
#pragma once
#include <stdint.h>

namespace core {

struct SessionGuardCfg {
    uint32_t idleMs = 10UL * 60 * 1000;    // нет байт столько — закрыть сессию
    uint32_t linkDownGraceMs = 5UL * 1000;  // столько терпим пропажу сети, прежде чем закрыть
};

// Почему сторож требует закрыть сессию; нужен для внятной строки в логе.
enum class SessionVerdict : uint8_t {
    Keep,
    Idle,      // клиент молчит слишком долго
    LinkDown,  // сети нет — живого клиента на том конце быть не может
};

class SessionGuard {
   public:
    void configure(const SessionGuardCfg& cfg) { cfg_ = cfg; }

    void onOpen(uint32_t nowMs) {
        open_ = true;
        lastTrafficMs_ = nowMs;
        linkDown_ = false;
    }
    // Байты в любую сторону: сессия жива.
    void onTraffic(uint32_t nowMs) { lastTrafficMs_ = nowMs; }
    void onClose() { open_ = false; }

    bool open() const { return open_; }
    uint32_t idleMs(uint32_t nowMs) const { return open_ ? nowMs - lastTrafficMs_ : 0; }

    SessionVerdict check(uint32_t nowMs, bool linkUp) {
        if (!open_) {
            linkDown_ = false;
            return SessionVerdict::Keep;
        }
        // Сеть пропала: сокет клиента уже мёртв, но recv() об этом не узнает.
        // Короткое мигание не в счёт — признак «связь есть» бывает и ложным
        // (espressif/arduino-esp32#12714), а рвать рабочую сессию из-за одной
        // итерации loop() нельзя.
        if (linkUp) {
            linkDown_ = false;
        } else {
            if (!linkDown_) {
                linkDown_ = true;
                linkDownSinceMs_ = nowMs;
            }
            if (nowMs - linkDownSinceMs_ >= cfg_.linkDownGraceMs) return SessionVerdict::LinkDown;
        }
        if (cfg_.idleMs && nowMs - lastTrafficMs_ >= cfg_.idleMs) return SessionVerdict::Idle;
        return SessionVerdict::Keep;
    }

   private:
    SessionGuardCfg cfg_;
    bool open_ = false;
    uint32_t lastTrafficMs_ = 0;
    bool linkDown_ = false;
    uint32_t linkDownSinceMs_ = 0;
};

}  // namespace core
