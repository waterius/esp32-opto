#include "opto_esp32.h"

#include <Arduino.h>
#include <HardwareSerial.h>

#ifndef OPTO_RX_PIN
#define OPTO_RX_PIN 17
#endif
#ifndef OPTO_TX_PIN
#define OPTO_TX_PIN 18
#endif

OptoEsp32 opto;

namespace {

HardwareSerial uart(1);

// Слово конфигурации UART из «человеческих» параметров.
uint32_t configWord(const core::SerialCfg& c) {
    const bool two = c.stop == 2;
    switch (c.bits) {
        case 7:
            if (c.parity == 'E') return two ? SERIAL_7E2 : SERIAL_7E1;
            if (c.parity == 'O') return two ? SERIAL_7O2 : SERIAL_7O1;
            return two ? SERIAL_7N2 : SERIAL_7N1;
        case 6:
            if (c.parity == 'E') return two ? SERIAL_6E2 : SERIAL_6E1;
            if (c.parity == 'O') return two ? SERIAL_6O2 : SERIAL_6O1;
            return two ? SERIAL_6N2 : SERIAL_6N1;
        case 5:
            if (c.parity == 'E') return two ? SERIAL_5E2 : SERIAL_5E1;
            if (c.parity == 'O') return two ? SERIAL_5O2 : SERIAL_5O1;
            return two ? SERIAL_5N2 : SERIAL_5N1;
        default:
            if (c.parity == 'E') return two ? SERIAL_8E2 : SERIAL_8E1;
            if (c.parity == 'O') return two ? SERIAL_8O2 : SERIAL_8O1;
            return two ? SERIAL_8N2 : SERIAL_8N1;
    }
}

}  // namespace

void OptoEsp32::begin(const core::SerialCfg& cfg) {
    cfg_ = cfg;
    uart.begin(cfg.baud, configWord(cfg), OPTO_RX_PIN, OPTO_TX_PIN);
    started_ = true;
}

void OptoEsp32::configure(const core::SerialCfg& cfg) {
    if (started_ && cfg_.baud == cfg.baud && cfg_.bits == cfg.bits && cfg_.parity == cfg.parity &&
        cfg_.stop == cfg.stop)
        return;
    if (started_) uart.end();
    begin(cfg);
}

size_t OptoEsp32::write(const uint8_t* data, size_t len) { return uart.write(data, len); }
int OptoEsp32::read() { return uart.read(); }
int OptoEsp32::available() { return uart.available(); }

void OptoEsp32::flushInput() {
    while (uart.available()) uart.read();
}
