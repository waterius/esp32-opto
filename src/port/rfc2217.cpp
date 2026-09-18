#include "rfc2217.h"

#include <Arduino.h>
#include <esp_pthread.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>

#include <atomic>

#include "../core/session_guard.h"
#include "log.h"
#include "net.h"
#include "opto_bus.h"
#include "rfc2217_server.h"

namespace rfc2217 {
namespace {

const size_t FROM_CLIENT_BUFFER = 1024;

rfc2217_server_t server = nullptr;
StreamBufferHandle_t fromClient = nullptr;  // клиент → UART: пишет поток сервера, читает loop()
std::atomic<bool> connected{false};

// Сторож сессии: пока она открыта, опрос счётчика и облако стоят, поэтому
// «открыта навсегда» недопустимо. Считает loop(), решает ядро.
core::SessionGuard guard;
bool disconnectAsked = false;

// Признаки жизни клиента из потоков сервера: данные и команды RFC 2217.
// loop() сравнивает со своим прошлым значением — так телнет-переговоры без
// единого байта данных тоже считаются активностью.
std::atomic<uint32_t> activity{0};
uint32_t seenActivity = 0;

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
    activity.fetch_add(1);
    xStreamBufferSend(fromClient, data, len, 0);  // не влезло — теряем, как переполненный UART
}

// Верхняя граница — как в списке допустимых скоростей на странице настроек
// (BAUDS в web.cpp). opto_bus.pack() всё равно режет скорость до 24 бит —
// без этой границы snapshot() мог бы показать не то, что запросил клиент.
const uint32_t MAX_BAUD = 115200;

// Значение 0 во всех SET-командах RFC 2217 — «сообщите текущее».
unsigned onBaudrate(void*, unsigned requested) {
    activity.fetch_add(1);
    if (requested) {
        uint32_t baud = requested > MAX_BAUD ? MAX_BAUD : requested;
        wantBaud.store(baud);
        return baud;
    }
    uint32_t w = wantBaud.load();
    return w ? w : bus.snapshot().baud;
}

unsigned onDatasize(void*, unsigned requested) {
    activity.fetch_add(1);
    if (requested >= 5 && requested <= 8) {
        wantBits.store((uint8_t)requested);
        return requested;
    }
    uint8_t w = wantBits.load();
    return w ? w : bus.snapshot().bits;
}

// RFC 2217: 1 — нет, 2 — нечётность, 3 — чётность. MARK и SPACE не поддерживаем.
unsigned onParity(void*, unsigned requested) {
    activity.fetch_add(1);
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
    activity.fetch_add(1);
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
    // task_stack_size/task_priority/task_core_id библиотека не читает (создаёт
    // поток через pthread_create с атрибутами по умолчанию) — стек задаём ниже
    // через esp_pthread, здесь эти поля оставлять незачем.

    // Стек pthread по умолчанию в arduino-esp32 — 2048 байт, этого мало и
    // серверному потоку, и потоку приёма клиента, который он порождает из
    // себя (буферы на стеке по 128-256 байт). inherit_cfg распространяет
    // размер на этот дочерний поток тоже.
    esp_pthread_cfg_t defaultCfg = esp_pthread_get_default_config();
    esp_pthread_cfg_t pcfg = defaultCfg;
    pcfg.stack_size = 4096;
    pcfg.inherit_cfg = true;
    esp_pthread_set_cfg(&pcfg);
    bool failed = rfc2217_server_create(&cfg, &server) != 0 || rfc2217_server_start(server) != 0;
    esp_pthread_set_cfg(&defaultCfg);  // на уже созданные потоки не влияет, возвращает cfg только этому

    if (failed) {
        Log.println("RFC 2217: сервер не запустился");
        server = nullptr;
        return;
    }
    Log.printf("RFC 2217: порт %u\n", port);
}

void loop() {
    if (!server) return;
    uint32_t now = millis();

    if (connected.load()) {
        if (bus.owner() != BusOwner::Transparent) {
            bus.beginTransparent();
            guard.onOpen(now);
            seenActivity = activity.load();
            disconnectAsked = false;
            Log.println("RFC 2217: клиент подключился, опрос счётчика остановлен");
        }
    } else if (bus.owner() == BusOwner::Transparent || bus.abortRequested()) {
        bus.endTransparent();
        guard.onClose();
        disconnectAsked = false;
        xStreamBufferReset(fromClient);
        Log.println("RFC 2217: клиент отключился, порт свободен");
    }
    if (bus.owner() != BusOwner::Transparent) return;

    uint32_t seen = activity.load();
    if (seen != seenActivity) {
        seenActivity = seen;
        guard.onTraffic(now);
    }

    // Клиент мог исчезнуть, не закрыв сокет: ноутбук уснул, отвалился Wi-Fi,
    // NAT забыл трансляцию. TCP об этом сам не узнает, а сессия держит
    // оптопорт — и устройство перестаёт ходить в облако. Закрываем сами.
    core::SessionVerdict verdict = guard.check(now, net::connected());
    if (verdict != core::SessionVerdict::Keep && !disconnectAsked) {
        disconnectAsked = true;
        Log.printf("RFC 2217: закрываем сессию — %s\n",
                   verdict == core::SessionVerdict::Idle ? "клиент молчит слишком долго"
                                                         : "пропала сеть");
        rfc2217_server_disconnect_client(server);
    }

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
    if (n) {
        guard.onTraffic(now);  // счётчик отвечает — сессия точно рабочая
        rfc2217_server_send_data(server, buf, n);
    }
}

uint32_t idleSeconds() { return guard.idleMs(millis()) / 1000; }

bool active() { return connected.load() && bus.owner() == BusOwner::Transparent; }

}  // namespace rfc2217
