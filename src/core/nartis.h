// Ядро: адаптер счётчика НАРТИС (СПОДЭС / DLMS-COSEM).
#pragma once
#include "dlms.h"
#include "meter.h"

namespace core {

class NartisMeter : public IMeter {
   public:
    explicit NartisMeter(IOptoPort& port) : dlms_(port) {}

    // addr = 0 → перебрать типовые адреса (16 у серии 100/300, 17 у серии И).
    void setAddress(uint8_t addr) { addr_ = addr; }
    // Пароль LLS. Перебирать пароли нельзя: после 5 неверных попыток
    // счётчик блокирует интерфейсы на сутки.
    void setPassword(const char* pwd);

    bool read(MeterData& out) override;
    // Умолчания SerialCfg и есть параметры НАРТИС: 9600 8N1.
    SerialCfg defaultSerial() const override { return SerialCfg(); }

    // Адрес, на котором счётчик отозвался в последний раз.
    uint8_t foundAddress() const { return found_; }

   private:
    bool readAll(MeterData& out);
    bool readRegister(const uint8_t obis[6], double& kwh);
    void readString(const uint8_t obis[6], char* out, size_t cap);

    DlmsClient dlms_;
    uint8_t addr_ = 0;
    uint8_t found_ = 0;
    char pwd_[17] = "111";
};

}  // namespace core
