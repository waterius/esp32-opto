// Ядро: защита от кирпича после неудачной прошивки.
//
// Облачный OTA пишет образ и перезагружается. Если новая прошивка не поднимает
// Wi-Fi, вернуть её можно только по проводу — у C3 через USB-вилку головки
// (docs/07). Поэтому считаем загрузки подряд, не дожившие до признака «всё
// хорошо»: набралось слишком много — стартуем в усечённом режиме, где работают
// только сеть, веб-страницы и обновление.
//
// Тот же приём, что safe_mode у ESPHome: там по умолчанию 10 неудачных
// загрузок и «boot is good after 1 min», счётчик — во флеше.
#pragma once
#include <stdint.h>

namespace core {

struct BootGuardCfg {
    uint8_t safeAfter = 5;            // столько загрузок подряд без успеха — усечённый режим
    // Столько продержались — загрузка удалась. Минуты достаточно: к этому
    // моменту подняты Wi-Fi и веб-сервер, то есть всё, что защищает усечённый
    // режим. Столько же ждёт ESPHome («boot is good after 1 min»). Больше
    // ставить вредно: при отладке плату передёргивают чаще, и она уходит в
    // усечённый режим на ровном месте.
    uint32_t goodAfterMs = 60UL * 1000;
};

class BootGuard {
   public:
    void configure(const BootGuardCfg& cfg) { cfg_ = cfg; }

    // Старт прошивки: stored — счётчик из NVS. Возвращает новое значение,
    // которое надо туда записать.
    uint8_t onBoot(uint8_t stored) {
        count_ = stored < 255 ? (uint8_t)(stored + 1) : 255;
        safe_ = cfg_.safeAfter && count_ >= cfg_.safeAfter;
        cleared_ = false;
        return count_;
    }

    bool safeMode() const { return safe_; }
    uint8_t bootCount() const { return count_; }

    // true ровно один раз, когда прошивка продержалась достаточно долго:
    // счётчик пора обнулить в NVS. Обычная перезагрузка (смена настроек, OTA,
    // потеря сети на час) случается уже после этого и в счётчик не попадает.
    bool takeBootIsGood(uint32_t uptimeMs) {
        if (cleared_ || uptimeMs < cfg_.goodAfterMs) return false;
        cleared_ = true;
        return true;
    }

   private:
    BootGuardCfg cfg_;
    uint8_t count_ = 0;
    bool safe_ = false;
    bool cleared_ = false;
};

}  // namespace core
