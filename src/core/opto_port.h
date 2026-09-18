// Ядро: интерфейс оптопорта. Реализация — port/opto_bus.*.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace core {

// Параметры COM-порта. parity: 'N', 'E', 'O'.
struct SerialCfg {
    uint32_t baud = 9600;
    uint8_t bits = 8;
    char parity = 'N';
    uint8_t stop = 1;
};

inline bool operator==(const SerialCfg& a, const SerialCfg& b) {
    return a.baud == b.baud && a.bits == b.bits && a.parity == b.parity && a.stop == b.stop;
}

class IOptoPort {
   public:
    virtual ~IOptoPort() {}
    virtual void configure(const SerialCfg& cfg) = 0;
    virtual size_t write(const uint8_t* data, size_t len) = 0;
    virtual int read() = 0;  // -1, если пусто
    virtual int available() = 0;
    virtual void flushInput() = 0;
    // Порт забрала прозрачная сессия: текущий обмен надо бросить сразу.
    virtual bool abortRequested() = 0;
};

// Время — тоже из порта, чтобы ядро не зависело от Arduino.
uint32_t nowMs();
void sleepMs(uint32_t ms);

}  // namespace core
