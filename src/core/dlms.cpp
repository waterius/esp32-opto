#include "dlms.h"

#include <stdio.h>
#include <string.h>

namespace core {
namespace {

const uint8_t FLAG = 0x7E;
const uint8_t LLC_REQ[3] = {0xE6, 0xE6, 0x00};
const uint8_t LLC_RSP[3] = {0xE6, 0xE7, 0x00};
const uint32_t FRAME_TIMEOUT_MS = 3000;

// CRC-16/X-25, как в HDLC
uint16_t fcs16(const uint8_t* p, size_t len) {
    uint16_t fcs = 0xFFFF;
    while (len--) {
        fcs ^= *p++;
        for (int i = 0; i < 8; i++) fcs = (fcs & 1) ? (uint16_t)((fcs >> 1) ^ 0x8408) : (uint16_t)(fcs >> 1);
    }
    return (uint16_t)(fcs ^ 0xFFFF);
}

}  // namespace

bool DlmsClient::fail(const char* msg) {
    snprintf(err_, sizeof(err_), "%s", msg);
    return false;
}

bool DlmsClient::sendFrame(uint8_t control, const uint8_t* info, size_t infoLen) {
    uint8_t f[DLMS_MAX_PDU + 32];
    size_t frameLen = 2 + 2 + 1 + 1 + 2 + (infoLen ? infoLen + 2 : 0);
    if (frameLen + 2 > sizeof(f)) return fail("кадр слишком длинный");

    size_t n = 0;
    f[n++] = FLAG;
    size_t start = n;  // с этого байта считается длина и контрольные суммы
    f[n++] = (uint8_t)(0xA0 | ((frameLen >> 8) & 0x07));
    f[n++] = (uint8_t)(frameLen & 0xFF);
    f[n++] = dst_[0];
    f[n++] = dst_[1];
    f[n++] = src_;
    f[n++] = control;
    uint16_t hcs = fcs16(&f[start], n - start);
    f[n++] = (uint8_t)(hcs & 0xFF);
    f[n++] = (uint8_t)(hcs >> 8);
    if (infoLen) {
        memcpy(&f[n], info, infoLen);
        n += infoLen;
        uint16_t crc = fcs16(&f[start], n - start);
        f[n++] = (uint8_t)(crc & 0xFF);
        f[n++] = (uint8_t)(crc >> 8);
    }
    f[n++] = FLAG;
    return port_.write(f, n) == n;
}

bool DlmsClient::recvFrame(uint8_t& control, uint8_t* info, size_t cap, size_t& infoLen,
                           bool& segmented, uint32_t timeoutMs) {
    uint8_t buf[DLMS_MAX_PDU + 32];
    size_t n = 0;
    size_t frameLen = 0;
    uint32_t deadline = nowMs() + timeoutMs;

    // открывающий флаг
    while (true) {
        if (nowMs() > deadline) return fail("нет ответа");
        int c = port_.read();
        if (c < 0) {
            sleepMs(2);
            continue;
        }
        if (c == FLAG) break;
    }
    // тело кадра до закрывающего флага
    while (true) {
        if (nowMs() > deadline) return fail("обрыв кадра");
        int c = port_.read();
        if (c < 0) {
            sleepMs(2);
            continue;
        }
        if (n == 0 && c == FLAG) continue;  // сдвоенные флаги между кадрами
        if (n >= sizeof(buf)) return fail("кадр не влез");
        buf[n++] = (uint8_t)c;
        if (n == 2) frameLen = (size_t)(((buf[0] & 0x07) << 8) | buf[1]);
        if (n >= 2 && n == frameLen) break;
    }
    if (n < 9) return fail("короткий кадр");
    segmented = (buf[0] & 0x08) != 0;

    uint16_t hcs = fcs16(buf, 6);
    if ((uint8_t)(hcs & 0xFF) != buf[6] || (uint8_t)(hcs >> 8) != buf[7]) return fail("HCS не сошёлся");
    control = buf[5];

    infoLen = 0;
    if (n > 10) {
        size_t len = n - 8 - 2;
        uint16_t crc = fcs16(buf, n - 2);
        if ((uint8_t)(crc & 0xFF) != buf[n - 2] || (uint8_t)(crc >> 8) != buf[n - 1])
            return fail("FCS не сошёлся");
        if (len > cap) return fail("данные не влезли");
        memcpy(info, &buf[8], len);
        infoLen = len;
    }
    // закрывающий флаг
    for (int i = 0; i < 50; i++) {
        int c = port_.read();
        if (c == FLAG) break;
        if (c < 0) sleepMs(1);
    }
    return true;
}

bool DlmsClient::sendPdu(const uint8_t* pdu, size_t len, uint8_t* resp, size_t cap, size_t& respLen) {
    uint8_t info[DLMS_MAX_PDU + 8];
    if (len + 3 > sizeof(info)) return fail("PDU слишком длинный");
    memcpy(info, LLC_REQ, 3);
    memcpy(&info[3], pdu, len);

    uint8_t control = (uint8_t)((rs_ << 5) | 0x10 | (ss_ << 1));
    if (!sendFrame(control, info, len + 3)) return false;
    ss_ = (uint8_t)((ss_ + 1) & 7);

    respLen = 0;
    bool segmented = true;
    bool first = true;
    while (segmented) {
        uint8_t rctl = 0;
        size_t n = 0;
        if (!recvFrame(rctl, info, sizeof(info), n, segmented, FRAME_TIMEOUT_MS)) return false;
        if ((rctl & 0x01) == 0) rs_ = (uint8_t)((rs_ + 1) & 7);  // I-кадр
        size_t off = 0;
        if (first) {
            if (n < 3 || memcmp(info, LLC_RSP, 3) != 0) return fail("нет LLC в ответе");
            off = 3;
            first = false;
        }
        if (respLen + (n - off) > cap) return fail("ответ не влез");
        memcpy(&resp[respLen], &info[off], n - off);
        respLen += n - off;
        if (segmented) {
            // подтверждаем сегмент и просим следующий
            if (!sendFrame((uint8_t)((rs_ << 5) | 0x10 | 0x01), nullptr, 0)) return false;
        }
    }
    return true;
}

bool DlmsClient::connect(uint8_t phys, uint8_t client, const char* pwd) {
    connected_ = false;
    ss_ = 0;
    rs_ = 0;
    dst_[0] = 0x02;                          // логический адрес 1
    dst_[1] = (uint8_t)((phys << 1) | 0x01);  // физический адрес
    src_ = (uint8_t)((client << 1) | 0x01);
    port_.flushInput();

    // SNRM без согласования параметров
    if (!sendFrame(0x93, nullptr, 0)) return false;
    uint8_t buf[DLMS_MAX_PDU];
    uint8_t ctl = 0;
    size_t n = 0;
    bool seg = false;
    if (!recvFrame(ctl, buf, sizeof(buf), n, seg, FRAME_TIMEOUT_MS)) return false;
    if ((ctl & 0xEF) != 0x63) return fail("вместо UA пришло другое");

    // AARQ
    size_t pwdLen = pwd ? strlen(pwd) : 0;
    if (pwdLen > 16) pwdLen = 16;
    uint8_t aarq[80];
    size_t k = 0;
    aarq[k++] = 0x60;
    size_t lenPos = k++;
    const uint8_t ctx[] = {0xA1, 0x09, 0x06, 0x07, 0x60, 0x85, 0x74, 0x05, 0x08, 0x01, 0x01};
    memcpy(&aarq[k], ctx, sizeof(ctx));
    k += sizeof(ctx);
    if (pwdLen) {
        const uint8_t auth[] = {0x8A, 0x02, 0x07, 0x80,                                      // ACSE requirements
                                0x8B, 0x07, 0x60, 0x85, 0x74, 0x05, 0x08, 0x02, 0x01};       // механизм LLS
        memcpy(&aarq[k], auth, sizeof(auth));
        k += sizeof(auth);
        aarq[k++] = 0xAC;
        aarq[k++] = (uint8_t)(pwdLen + 2);
        aarq[k++] = 0x80;
        aarq[k++] = (uint8_t)pwdLen;
        memcpy(&aarq[k], pwd, pwdLen);
        k += pwdLen;
    }
    const uint8_t init[] = {0xBE, 0x10, 0x04, 0x0E, 0x01, 0x00, 0x00, 0x00, 0x06,
                            0x5F, 0x1F, 0x04, 0x00, 0x00, 0x7E, 0x1F, 0x04, 0xB0};
    memcpy(&aarq[k], init, sizeof(init));
    k += sizeof(init);
    aarq[lenPos] = (uint8_t)(k - 2);

    size_t rn = 0;
    if (!sendPdu(aarq, k, buf, sizeof(buf), rn)) return false;
    if (rn < 2 || buf[0] != 0x61) return fail("вместо AARE пришло другое");
    // ищем result = accepted: A2 03 02 01 00
    for (size_t i = 0; i + 4 < rn; i++) {
        if (buf[i] == 0xA2 && buf[i + 1] == 0x03 && buf[i + 2] == 0x02 && buf[i + 3] == 0x01) {
            if (buf[i + 4] != 0x00) return fail("счётчик отказал в ассоциации");
            connected_ = true;
            return true;
        }
    }
    return fail("не понял AARE");
}

void DlmsClient::disconnect() {
    if (connected_) sendFrame(0x53, nullptr, 0);  // DISC
    uint8_t buf[64];
    uint8_t ctl;
    size_t n;
    bool seg;
    recvFrame(ctl, buf, sizeof(buf), n, seg, 500);
    connected_ = false;
}

bool DlmsClient::get(uint16_t classId, const uint8_t obis[6], uint8_t attr, uint8_t* out,
                     size_t outCap, size_t& outLen) {
    if (!connected_) return fail("нет связи");
    uint8_t pdu[16];
    size_t k = 0;
    pdu[k++] = 0xC0;  // get-request
    pdu[k++] = 0x01;  // normal
    pdu[k++] = 0xC1;  // invoke-id
    pdu[k++] = (uint8_t)(classId >> 8);
    pdu[k++] = (uint8_t)(classId & 0xFF);
    memcpy(&pdu[k], obis, 6);
    k += 6;
    pdu[k++] = attr;
    pdu[k++] = 0x00;  // без selective access

    uint8_t resp[DLMS_MAX_PDU];
    size_t rn = 0;
    if (!sendPdu(pdu, k, resp, sizeof(resp), rn)) return false;
    if (rn < 5 || resp[0] != 0xC4) return fail("не get-response");
    if (resp[1] != 0x01) return fail("ответ блоками не поддержан");
    if (resp[3] != 0x00) {
        snprintf(err_, sizeof(err_), "счётчик вернул ошибку %u", resp[4]);
        return false;
    }
    outLen = rn - 4;
    if (outLen > outCap) return fail("значение не влезло");
    memcpy(out, &resp[4], outLen);
    return true;
}

bool DlmsClient::parseNumber(const uint8_t* d, size_t len, double& v) {
    if (len < 2) return false;
    uint8_t tag = d[0];
    const uint8_t* p = d + 1;
    size_t n = len - 1;
    int64_t sv = 0;
    uint64_t uv = 0;
    size_t sz = 0;
    bool sign = false;
    switch (tag) {
        case 0x0F: sz = 1; sign = true; break;   // integer
        case 0x10: sz = 2; sign = true; break;   // long
        case 0x05: sz = 4; sign = true; break;   // double-long
        case 0x14: sz = 8; sign = true; break;   // long64
        case 0x11: sz = 1; break;                // unsigned
        case 0x12: sz = 2; break;                // long-unsigned
        case 0x06: sz = 4; break;                // double-long-unsigned
        case 0x15: sz = 8; break;                // long64-unsigned
        case 0x16: sz = 1; break;                // enum
        default: return false;
    }
    if (n < sz) return false;
    for (size_t i = 0; i < sz; i++) uv = (uv << 8) | p[i];
    if (sign) {
        sv = (int64_t)uv;
        if (sz < 8 && (uv & (1ULL << (sz * 8 - 1)))) sv = (int64_t)(uv | (~0ULL << (sz * 8)));
        v = (double)sv;
    } else {
        v = (double)uv;
    }
    return true;
}

bool DlmsClient::parseString(const uint8_t* d, size_t len, char* out, size_t cap) {
    if (len < 2) return false;
    if (d[0] != 0x09 && d[0] != 0x0A) return false;  // octet-string / visible-string
    size_t n = d[1];
    if (n + 2 > len) return false;
    size_t k = 0;
    for (size_t i = 0; i < n && k + 1 < cap; i++) {
        uint8_t c = d[2 + i];
        out[k++] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
    }
    out[k] = 0;
    return true;
}

bool DlmsClient::parseScalerUnit(const uint8_t* d, size_t len, int8_t& scaler, uint8_t& unit) {
    // structure { integer scaler, enum unit }
    if (len < 6 || d[0] != 0x02 || d[1] != 0x02) return false;
    if (d[2] != 0x0F || d[4] != 0x16) return false;
    scaler = (int8_t)d[3];
    unit = d[5];
    return true;
}

bool DlmsClient::parseDateTime(const uint8_t* d, size_t len, char* out, size_t cap) {
    if (len < 14 || d[0] != 0x09 || d[1] < 12) return false;
    const uint8_t* p = d + 2;
    uint16_t year = (uint16_t)((p[0] << 8) | p[1]);
    snprintf(out, cap, "%04u-%02u-%02u %02u:%02u:%02u", year, p[2], p[3], p[5], p[6], p[7]);
    return true;
}

}  // namespace core
