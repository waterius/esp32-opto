// Ядро: минимальный клиент DLMS/COSEM поверх HDLC (СПОДЭС).
// Умеет ровно то, что нужно для чтения: SNRM/UA, AARQ/AARE с паролем LLS,
// GET одного атрибута, DISC. Ни записи, ни шифрования, ни блоков.
#pragma once
#include "opto_port.h"

namespace core {

const size_t DLMS_MAX_PDU = 512;

class DlmsClient {
   public:
    explicit DlmsClient(IOptoPort& port) : port_(port) {}

    // phys — физический адрес счётчика, client — адрес клиента (32 = считыватель),
    // pwd — пароль LLS (nullptr или "" → без аутентификации).
    bool connect(uint8_t phys, uint8_t client, const char* pwd);
    void disconnect();

    // Читает атрибут. Возвращает сырые данные A-XDR в out.
    bool get(uint16_t classId, const uint8_t obis[6], uint8_t attr, uint8_t* out,
             size_t outCap, size_t& outLen);

    const char* lastError() const { return err_; }

    // Разбор A-XDR
    static bool parseNumber(const uint8_t* d, size_t len, double& v);
    static bool parseString(const uint8_t* d, size_t len, char* out, size_t cap);
    static bool parseScalerUnit(const uint8_t* d, size_t len, int8_t& scaler, uint8_t& unit);
    static bool parseDateTime(const uint8_t* d, size_t len, char* out, size_t cap);

   private:
    bool sendFrame(uint8_t control, const uint8_t* info, size_t infoLen);
    bool recvFrame(uint8_t& control, uint8_t* info, size_t cap, size_t& infoLen,
                   bool& segmented, uint32_t timeoutMs);
    bool sendPdu(const uint8_t* pdu, size_t len, uint8_t* resp, size_t cap, size_t& respLen);
    bool fail(const char* msg);

    IOptoPort& port_;
    uint8_t dst_[2] = {0x02, 0x23};  // logical 1, physical 17
    uint8_t src_ = 0x41;             // client 32
    uint8_t ss_ = 0;                 // наш счётчик отправки
    uint8_t rs_ = 0;                 // счётчик принятых кадров
    bool connected_ = false;
    char err_[48] = {0};
};

}  // namespace core
