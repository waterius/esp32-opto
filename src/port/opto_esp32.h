// Порт: UART1 оптопорта. Пины — флаги OPTO_RX_PIN / OPTO_TX_PIN в platformio.ini.
// Напрямую им пользуется только OptoBus (opto_bus.h).
#pragma once
#include <stddef.h>

#include "../core/opto_port.h"

class OptoEsp32 {
   public:
    void begin(const core::SerialCfg& cfg);
    void configure(const core::SerialCfg& cfg);
    size_t write(const uint8_t* data, size_t len);
    int read();
    int available();
    void flushInput();

    const core::SerialCfg& current() const { return cfg_; }

   private:
    core::SerialCfg cfg_;
    bool started_ = false;
};
