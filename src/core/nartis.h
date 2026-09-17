// Ядро: адаптер счётчика НАРТИС (СПОДЭС = DLMS/COSEM поверх HDLC) на GuruxDLMS.c.
#pragma once
#include "meter.h"

namespace core {

class NartisMeter : public IMeter {
   public:
    explicit NartisMeter(IOptoPort& port) : port_(port) {}

    // 0 — перебрать 16 (серия 100/300), затем 17 (серия И).
    void setAddress(uint8_t addr) { addr_ = addr; }
    // Пароль LLS. Перебирать пароли нельзя: после 5 неверных попыток
    // счётчик блокирует интерфейсы на сутки.
    void setPassword(const char* pwd);

    ReadResult read(MeterData& out, char* error, size_t errorCap) override;

    // Адрес, на котором счётчик отозвался в последний раз.
    uint8_t foundAddress() const { return found_; }

   private:
    IOptoPort& port_;
    uint8_t addr_ = 0;
    uint8_t found_ = 0;  // адрес, на котором счётчик ответил в прошлый раз
    char pwd_[17] = "111";
};

}  // namespace core
