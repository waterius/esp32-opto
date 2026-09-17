// Порт: единственный владелец оптопорта (UART1) — арбитр опроса и прозрачной сессии.
// Все байты, прошедшие через шину, пишутся в лог строками TX/RX в hex.
// Все методы, кроме requestPreempt(), вызываются только из loop().
#pragma once
#include <atomic>

#include "../core/opto_port.h"

enum class BusOwner : uint8_t { Free, Meter, Transparent };

class OptoBus : public core::IOptoPort {
   public:
    void begin(const core::SerialCfg& cfg);

    // IOptoPort — для адаптера счётчика
    void configure(const core::SerialCfg& cfg) override;
    size_t write(const uint8_t* data, size_t len) override;
    int read() override;
    int available() override;
    void flushInput() override;
    bool abortRequested() override { return preempt_.load(); }

    const core::SerialCfg& current() const;
    BusOwner owner() const { return owner_; }

    // Опрос счётчика: взять шину, только если она свободна и никто не ждёт.
    bool acquireForMeter();
    void releaseMeter();

    // Прозрачная сессия.
    void beginTransparent();
    void endTransparent();  // заодно снимает запрос на прерывание

    // Из колбэка подключения клиента (поток сервера RFC 2217):
    // идущий обмен со счётчиком должен прерваться.
    void requestPreempt() { preempt_.store(true); }

   private:
    BusOwner owner_ = BusOwner::Free;
    std::atomic<bool> preempt_{false};
};

extern OptoBus bus;
