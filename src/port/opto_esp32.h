// Порт: оптопорт на аппаратном UART1.
// Пины задаются в platformio.ini: OPTO_RX_PIN / OPTO_TX_PIN.
#pragma once
#include "../core/opto_port.h"

class OptoEsp32 : public core::IOptoPort {
   public:
    void begin(const core::SerialCfg& cfg);
    void configure(const core::SerialCfg& cfg) override;
    size_t write(const uint8_t* data, size_t len) override;
    int read() override;
    int available() override;
    void flushInput() override;

    const core::SerialCfg& current() const { return cfg_; }

   private:
    core::SerialCfg cfg_;
    bool started_ = false;
};

extern OptoEsp32 opto;
