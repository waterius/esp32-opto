// Ядро: контроль живости сети.
//
// Драйвер умеет врать. Наблюдалось на плате: WiFi.status() говорил
// WL_CONNECTED, адрес был получен, wifi_drops не рос — а устройство не
// отвечало ни на HTTP, ни на ICMP больше десяти минут. Это известный дефект
// arduino-esp32 (#12714), и лестница восстановления на него не реагирует: с её
// точки зрения связь есть.
//
// Поэтому нужен второй источник правды — не слова драйвера, а факт того, что
// пакеты реально ходят. Порт периодически пингует шлюз; этот автомат решает,
// когда пинговать и когда признать связь мёртвой.
//
// Предохранитель. Объявлять связь мёртвой можно, только если хотя бы одна
// проверка когда-то прошла: иначе в сети, где шлюз не отвечает на ICMP,
// устройство ушло бы в вечный цикл переподключений и перезагрузок — а это
// хуже той болезни, которую мы лечим.
#pragma once
#include <stdint.h>

namespace core {

struct LinkGuardCfg {
    uint32_t quietMs = 60000;  // столько без подтверждений — пора проверить
    uint8_t deadAfter = 3;     // неудачных проверок подряд; 0 — не объявлять
};

class LinkGuard {
   public:
    void configure(const LinkGuardCfg& cfg) { cfg_ = cfg; }

    // Драйвер сообщил о смене состояния связи: отсчёт начинается заново.
    void reset(uint32_t nowMs) {
        failures_ = 0;
        lastOkMs_ = nowMs;
        lastProbeMs_ = nowMs;
    }

    // Обмен с сетью получился сам собой — например, облако ответило.
    // Проверку отодвигает, но сторож не взводит.
    void activity(uint32_t nowMs) {
        failures_ = 0;
        lastOkMs_ = nowMs;
    }

    // Проверка прошла. Только она взводит сторож: пока ни одна не удалась, мы
    // не знаем, работают ли проверки в этой сети вообще.
    void probeOk(uint32_t nowMs) {
        armed_ = true;
        failures_ = 0;
        lastOkMs_ = nowMs;
        lastProbeMs_ = nowMs;
    }

    void probeFailed(uint32_t nowMs) {
        lastProbeMs_ = nowMs;
        if (armed_ && failures_ < 255) ++failures_;
    }

    bool shouldProbe(uint32_t nowMs) const {
        return nowMs - lastOkMs_ >= cfg_.quietMs && nowMs - lastProbeMs_ >= cfg_.quietMs;
    }

    // Связь молчит, хотя драйвер уверяет в обратном.
    bool dead() const { return armed_ && cfg_.deadAfter && failures_ >= cfg_.deadAfter; }

    bool armed() const { return armed_; }
    uint8_t failures() const { return failures_; }

   private:
    LinkGuardCfg cfg_;
    bool armed_ = false;
    uint8_t failures_ = 0;
    uint32_t lastOkMs_ = 0;
    uint32_t lastProbeMs_ = 0;
};

}  // namespace core
