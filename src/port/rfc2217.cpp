#include "rfc2217.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>

#include <atomic>

#include "log.h"
#include "opto_bus.h"
#include "rfc2217_server.h"

namespace rfc2217 {
namespace {

const size_t FROM_CLIENT_BUFFER = 1024;

rfc2217_server_t server = nullptr;
StreamBufferHandle_t fromClient = nullptr;  // клиент → UART: пишет поток сервера, читает loop()
std::atomic<bool> connected{false};

// Запрошенные клиентом параметры порта; 0 — не менялись. Применяет loop().
std::atomic<uint32_t> wantBaud{0};
std::atomic<uint8_t> wantBits{0};
std::atomic<char> wantParity{0};
std::atomic<uint8_t> wantStop{0};

void onConnected(void*) {
    wantBaud.store(0);
    wantBits.store(0);
    wantParity.store(0);
    wantStop.store(0);
    bus.requestPreempt();  // идущий опрос счётчика прерывается сразу
    connected.store(true);
}

void onDisconnected(void*) { connected.store(false); }

void onData(void*, const uint8_t* data, size_t len) {
    xStreamBufferSend(fromClient, data, len, 0);  // не влезло — теряем, как переполненный UART
}

// Значение 0 во всех SET-командах RFC 2217 — «сообщите текущее».
unsigned onBaudrate(void*, unsigned requested) {
    if (requested) {
        wantBaud.store(requested);
        return requested;
    }
    uint32_t w = wantBaud.load();
    return w ? w : bus.snapshot().baud;
}

unsigned onDatasize(void*, unsigned requested) {
    if (requested >= 5 && requested <= 8) {
        wantBits.store((uint8_t)requested);
        return requested;
    }
    uint8_t w = wantBits.load();
    return w ? w : bus.snapshot().bits;
}

// RFC 2217: 1 — нет, 2 — нечётность, 3 — чётность. MARK и SPACE не поддерживаем.
unsigned onParity(void*, unsigned requested) {
    char p = requested == 1 ? 'N' : (requested == 2 ? 'O' : (requested == 3 ? 'E' : 0));
    if (p) {
        wantParity.store(p);
        return requested;
    }
    char c = wantParity.load();
    if (!c) c = bus.snapshot().parity;
    return c == 'O' ? 2 : (c == 'E' ? 3 : 1);
}

// RFC 2217: 1 — один стоп-бит, 2 — два. 1,5 не поддерживаем.
unsigned onStopsize(void*, unsigned requested) {
    if (requested == 1 || requested == 2) {
        wantStop.store((uint8_t)requested);
        return requested;
    }
    uint8_t w = wantStop.load();
    return w ? w : bus.snapshot().stop;
}

}  // namespace

void begin(uint16_t port) {
    fromClient = xStreamBufferCreate(FROM_CLIENT_BUFFER, 1);
    if (!fromClient) {
        Log.println("RFC 2217: не выделен буфер приёма, сервер не запущен");
        return;
    }
    rfc2217_server_config_t cfg = {};
    cfg.on_client_connected = onConnected;
    cfg.on_client_disconnected = onDisconnected;
    cfg.on_baudrate = onBaudrate;
    cfg.on_data_received = onData;
    cfg.on_datasize = onDatasize;
    cfg.on_parity = onParity;
    cfg.on_stopsize = onStopsize;
    cfg.port = port;
    cfg.task_stack_size = 4096;
    cfg.task_priority = 5;
    cfg.task_core_id = 0;
    if (rfc2217_server_create(&cfg, &server) != 0 || rfc2217_server_start(server) != 0) {
        Log.println("RFC 2217: сервер не запустился");
        server = nullptr;
        return;
    }
    Log.printf("RFC 2217: порт %u\n", port);
}

void loop() {
    if (!server) return;

    if (connected.load()) {
        if (bus.owner() != BusOwner::Transparent) {
            bus.beginTransparent();
            Log.println("RFC 2217: клиент подключился, опрос счётчика остановлен");
        }
    } else if (bus.owner() == BusOwner::Transparent || bus.abortRequested()) {
        bus.endTransparent();
        xStreamBufferReset(fromClient);
        Log.println("RFC 2217: клиент отключился, порт свободен");
    }
    if (bus.owner() != BusOwner::Transparent) return;

    // Параметры порта от клиента — до данных, пришедших после них
    core::SerialCfg cfg = bus.current();
    bool changed = false;
    uint32_t baud = wantBaud.exchange(0);
    if (baud) { cfg.baud = baud; changed = true; }
    uint8_t bits = wantBits.exchange(0);
    if (bits) { cfg.bits = bits; changed = true; }
    char parity = wantParity.exchange(0);
    if (parity) { cfg.parity = parity; changed = true; }
    uint8_t stop = wantStop.exchange(0);
    if (stop) { cfg.stop = stop; changed = true; }
    if (changed) bus.configure(cfg);

    uint8_t buf[128];
    size_t n;
    while ((n = xStreamBufferReceive(fromClient, buf, sizeof(buf), 0)) > 0) bus.write(buf, n);

    n = 0;
    while (n < sizeof(buf) && bus.available() > 0) {
        int c = bus.read();
        if (c < 0) break;
        buf[n++] = (uint8_t)c;
    }
    if (n) rfc2217_server_send_data(server, buf, n);
}

bool active() { return connected.load() && bus.owner() == BusOwner::Transparent; }

}  // namespace rfc2217
