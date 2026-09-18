#include "opto_bus.h"

#include "log.h"
#include "opto_esp32.h"

OptoBus bus;

namespace {

OptoEsp32 uart;

// Байты оптопорта в лог: строка на направление, не длиннее LINE байт.
// Принятые копятся, пока во входном буфере UART есть данные, — обычно строка = кадр.
const size_t LINE = 20;
uint8_t rxPending[LINE];
size_t rxCount = 0;

void logBytes(const char* dir, const uint8_t* data, size_t len) {
    char line[3 + LINE * 3 + 1];
    for (size_t off = 0; off < len; off += LINE) {
        size_t n = len - off < LINE ? len - off : LINE;
        int k = snprintf(line, sizeof(line), "%s", dir);
        for (size_t i = 0; i < n; ++i) k += snprintf(line + k, sizeof(line) - k, " %02x", data[off + i]);
        Log.println(line);
    }
}

void flushRx() {
    if (!rxCount) return;
    logBytes("RX", rxPending, rxCount);
    rxCount = 0;
}

}  // namespace

void OptoBus::begin(const core::SerialCfg& cfg) {
    uart.begin(cfg);
    snapshot_.store(pack(cfg));
}

void OptoBus::configure(const core::SerialCfg& cfg) {
    flushRx();
    if (!(cfg == uart.current()))
        Log.printf("Оптопорт: %lu %u%c%u\n", (unsigned long)cfg.baud, cfg.bits, cfg.parity, cfg.stop);
    uart.configure(cfg);
    snapshot_.store(pack(cfg));
}

size_t OptoBus::write(const uint8_t* data, size_t len) {
    flushRx();
    logBytes("TX", data, len);
    return uart.write(data, len);
}

int OptoBus::read() {
    int c = uart.read();
    if (c >= 0) {
        rxPending[rxCount++] = (uint8_t)c;
        if (rxCount == LINE) flushRx();
    }
    return c;
}

int OptoBus::available() {
    int n = uart.available();
    if (n <= 0) flushRx();  // входной буфер пуст — принятая порция закончилась
    return n;
}

void OptoBus::flushInput() {
    flushRx();
    uart.flushInput();
}

const core::SerialCfg& OptoBus::current() const { return uart.current(); }

// Упаковка cfg в одно 32-битное слово — читается/пишется атомарно одной
// инструкцией, поэтому snapshot() не гонится с configure() из другой задачи.
// Скорость (до 115200) — биты 0-23, биты данных (5-8, храним как bits-5) —
// 24-25, чётность (N=0,E=1,O=2) — 26-27, стоп-биты (1→0, 2→1) — бит 28.
uint32_t OptoBus::pack(const core::SerialCfg& cfg) {
    uint32_t bits = (cfg.bits >= 5 && cfg.bits <= 8) ? (uint32_t)(cfg.bits - 5) : 0;
    uint32_t parity = cfg.parity == 'E' ? 1u : (cfg.parity == 'O' ? 2u : 0u);
    uint32_t stop = cfg.stop == 2 ? 1u : 0u;
    return (cfg.baud & 0xFFFFFFu) | (bits << 24) | (parity << 26) | (stop << 28);
}

core::SerialCfg OptoBus::unpack(uint32_t v) {
    core::SerialCfg cfg;
    cfg.baud = v & 0xFFFFFFu;
    cfg.bits = (uint8_t)(((v >> 24) & 0x3u) + 5);
    uint32_t parity = (v >> 26) & 0x3u;
    cfg.parity = parity == 1 ? 'E' : (parity == 2 ? 'O' : 'N');
    cfg.stop = ((v >> 28) & 0x1u) ? 2 : 1;
    return cfg;
}

core::SerialCfg OptoBus::snapshot() const { return unpack(snapshot_.load()); }

bool OptoBus::acquireForMeter() {
    if (owner_ != BusOwner::Free || preempt_.load()) return false;
    owner_ = BusOwner::Meter;
    return true;
}

void OptoBus::releaseMeter() {
    flushRx();
    if (owner_ == BusOwner::Meter) owner_ = BusOwner::Free;
}

void OptoBus::beginTransparent() {
    flushRx();
    owner_ = BusOwner::Transparent;
    uart.flushInput();
}

void OptoBus::endTransparent() {
    flushRx();
    owner_ = BusOwner::Free;
    preempt_.store(false);
}
