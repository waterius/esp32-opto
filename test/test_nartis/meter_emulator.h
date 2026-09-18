// Оптопорты для тестов протокола: эмулятор счётчика и воспроизведение
// реального обмена. Оба работают на уровне байтов HDLC, поэтому не зависят
// от того, как устроен клиент DLMS внутри прошивки.
#pragma once
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "core/opto_port.h"
#include "real_exchange.h"

using Bytes = std::vector<uint8_t>;

// CRC-16/X-25, как в HDLC
inline uint16_t fcs16(const uint8_t* p, size_t n) {
    uint16_t fcs = 0xFFFF;
    while (n--) {
        fcs ^= *p++;
        for (int i = 0; i < 8; i++) fcs = (fcs & 1) ? (uint16_t)((fcs >> 1) ^ 0x8408) : (uint16_t)(fcs >> 1);
    }
    return (uint16_t)(fcs ^ 0xFFFF);
}

// "7e a0 08" → байты
inline Bytes hex(const char* s) {
    Bytes out;
    unsigned b;
    int used;
    while (sscanf(s, " %2x%n", &b, &used) == 1) {
        out.push_back((uint8_t)b);
        s += used;
    }
    return out;
}

inline std::string toHex(const Bytes& b) {
    std::string s;
    char h[4];
    for (size_t i = 0; i < b.size(); i++) {
        snprintf(h, sizeof(h), i ? " %02x" : "%02x", b[i]);
        s += h;
    }
    return s;
}

// Разобранный кадр HDLC. ok = флаги, длина, HCS и FCS сошлись.
struct Frame {
    bool ok = false;
    bool segmented = false;
    Bytes dst;
    Bytes src;
    uint8_t control = 0;
    Bytes info;
};

inline Frame parseFrame(const Bytes& f) {
    Frame r;
    if (f.size() < 10 || f.front() != 0x7E || f.back() != 0x7E) return r;
    Bytes body(f.begin() + 1, f.end() - 1);
    if ((body[0] & 0xF0) != 0xA0) return r;
    size_t len = (size_t)(((body[0] & 0x07) << 8) | body[1]);
    if (len != body.size()) return r;
    r.segmented = (body[0] & 0x08) != 0;
    size_t i = 2;
    while (i < len && !(body[i] & 1)) r.dst.push_back(body[i++]);
    if (i >= len) return r;
    r.dst.push_back(body[i++]);
    while (i < len && !(body[i] & 1)) r.src.push_back(body[i++]);
    if (i >= len) return r;
    r.src.push_back(body[i++]);
    if (i + 3 > len) return r;
    r.control = body[i++];
    if (fcs16(body.data(), i) != (uint16_t)(body[i] | (body[i + 1] << 8))) return r;
    i += 2;
    if (i < len) {
        if (i + 2 >= len || fcs16(body.data(), len - 2) != (uint16_t)(body[len - 2] | (body[len - 1] << 8)))
            return r;
        r.info.assign(body.begin() + i, body.end() - 2);
    }
    r.ok = true;
    return r;
}

// Кадр счётчик → клиент 32
inline Bytes serverFrame(uint8_t phys, uint8_t control, const Bytes& info, bool segmented) {
    Bytes body;
    size_t len = 2 + 1 + 2 + 1 + 2 + (info.empty() ? 0 : info.size() + 2);
    body.push_back((uint8_t)(0xA0 | (segmented ? 0x08 : 0) | ((len >> 8) & 0x07)));
    body.push_back((uint8_t)(len & 0xFF));
    body.push_back(0x41);
    body.push_back(0x02);
    body.push_back((uint8_t)((phys << 1) | 1));
    body.push_back(control);
    uint16_t hcs = fcs16(body.data(), body.size());
    body.push_back((uint8_t)hcs);
    body.push_back((uint8_t)(hcs >> 8));
    if (!info.empty()) {
        body.insert(body.end(), info.begin(), info.end());
        uint16_t crc = fcs16(body.data(), body.size());
        body.push_back((uint8_t)crc);
        body.push_back((uint8_t)(crc >> 8));
    }
    Bytes f{0x7E};
    f.insert(f.end(), body.begin(), body.end());
    f.push_back(0x7E);
    return f;
}

// Режет поток байтов от клиента на кадры по полю длины.
class FrameSplitter {
   public:
    std::vector<Bytes> push(const uint8_t* d, size_t n) {
        std::vector<Bytes> frames;
        buf_.insert(buf_.end(), d, d + n);
        while (true) {
            while (!buf_.empty() && buf_.front() != 0x7E) buf_.erase(buf_.begin());
            while (buf_.size() > 1 && buf_[1] == 0x7E) buf_.erase(buf_.begin());
            if (buf_.size() < 3) break;
            size_t len = (size_t)(((buf_[1] & 0x07) << 8) | buf_[2]);
            if (buf_.size() < len + 2) break;
            frames.emplace_back(buf_.begin(), buf_.begin() + len + 2);
            buf_.erase(buf_.begin(), buf_.begin() + len + 2);
        }
        return frames;
    }

   private:
    Bytes buf_;
};

// Ответы счётчика в очереди на чтение.
class ByteQueue : public core::IOptoPort {
   public:
    void configure(const core::SerialCfg&) override {}
    int read() override {
        if (out_.empty()) return -1;
        int c = out_.front();
        out_.pop_front();
        return c;
    }
    int available() override { return (int)out_.size(); }
    void flushInput() override { out_.clear(); }
    bool abortRequested() override { return false; }

   protected:
    void send(const Bytes& b) { out_.insert(out_.end(), b.begin(), b.end()); }
    std::deque<uint8_t> out_;
};

// Эмулятор НАРТИС-100: отвечает на SNRM, AARQ, GET и DISC как настоящий
// счётчик. Объекты и их данные A-XDR взяты из реального дампа.
class MeterEmulator : public ByteQueue {
   public:
    // Настройки поведения
    uint8_t phys = 16;             // физический адрес счётчика
    bool anyAddress = false;       // отвечать на любой адрес (для проверки отказа в пароле)
    bool silent = false;           // счётчик не отвечает вовсе
    std::string password = "111";  // пароль LLS клиента 32
    size_t maxInfo = 256;          // длиннее — ответ уходит сегментами
    int corruptFrames = 0;         // сколько ближайших ответов испортить (FCS)
    int abortAfterGets = -1;       // после скольких GET считать порт забранным прозрачной сессией (-1 — никогда)

    // Журнал: что прислал клиент
    std::vector<Bytes> frames;   // все кадры от клиента как есть
    std::vector<uint8_t> snrmAddrs;
    int badFrames = 0;           // кадры с неверной длиной или контрольной суммой
    int aarqs = 0;
    int gets = 0;
    int rrs = 0;
    int discs = 0;

    MeterEmulator() {
        // Данные атрибутов — байты из docs/06-nartis-100-exchange.md
        set(1, "0.0.96.1.0.255", 2, "09 08 35 32 32 30 37 38 33 39");
        set(1, "0.0.96.1.1.255", 2, "09 10 cd c0 d0 d2 c8 d1 2d 31 30 30 2e 31 32 31 52 4c");
        set(1, "0.0.96.1.2.255", 2, "09 06 32 35 35 2e 30 36");
        set(1, "0.0.96.1.3.255", 2, "09 0c 5a 61 76 6f 64 20 4e 61 72 74 69 73");
        set(8, "0.0.1.0.0.255", 2, "09 0c 07 ea 09 11 04 0c 0f 06 00 ff 4c 00");
        set(3, "1.0.1.8.0.255", 2, "06 00 19 5c 16");
        set(3, "1.0.1.8.1.255", 2, "06 00 11 8e 96");
        set(3, "1.0.1.8.2.255", 2, "06 00 07 cd 80");
        for (int t = 0; t <= 8; t++) {
            char obis[20];
            snprintf(obis, sizeof(obis), "1.0.1.8.%d.255", t);
            if (t >= 3) set(3, obis, 2, "06 00 00 00 00");
            set(3, obis, 3, "02 02 0f 00 16 1e");
        }
    }

    void set(uint16_t cls, const char* obis, uint8_t attr, const char* data) { objects_[key(cls, obis, attr)] = hex(data); }
    void remove(uint16_t cls, const char* obis, uint8_t attr) { objects_.erase(key(cls, obis, attr)); }

    // Имитация прозрачной сессии RFC 2217, забравшей порт после abortAfterGets GET-ов.
    bool abortRequested() override { return abortAfterGets >= 0 && gets >= abortAfterGets; }

    // Разобранные I-кадры клиента (запросы DLMS), по порядку
    std::vector<Frame> iFrames() const {
        std::vector<Frame> r;
        for (const Bytes& b : frames) {
            Frame f = parseFrame(b);
            if (f.ok && (f.control & 1) == 0) r.push_back(f);
        }
        return r;
    }

    size_t write(const uint8_t* d, size_t n) override {
        for (const Bytes& b : splitter_.push(d, n)) onFrame(b);
        return n;
    }

   private:
    static std::string key(uint16_t cls, const char* obis, uint8_t attr) {
        int a, b, c, dd, e, f;
        sscanf(obis, "%d.%d.%d.%d.%d.%d", &a, &b, &c, &dd, &e, &f);
        char k[40];
        snprintf(k, sizeof(k), "%u/%d.%d.%d.%d.%d.%d/%u", cls, a, b, c, dd, e, f, attr);
        return k;
    }

    void onFrame(const Bytes& raw) {
        frames.push_back(raw);
        Frame f = parseFrame(raw);
        if (!f.ok || f.dst.size() != 2 || f.src.size() != 1 || f.dst[0] != 0x02) {
            badFrames++;
            return;
        }
        uint8_t addr = f.dst[1] >> 1;
        if (f.control == 0x93) snrmAddrs.push_back(addr);
        if (silent || (addr != phys && !anyAddress)) return;
        addr_ = addr;

        if (f.control == 0x93) {  // SNRM → UA с параметрами, как у настоящего счётчика
            ns_ = nr_ = 0;
            segments_.clear();
            reply(serverFrame(addr_, 0x73, UA_PARAMS, false));
        } else if (f.control == 0x53) {  // DISC → UA
            discs++;
            reply(serverFrame(addr_, 0x73, UA_PARAMS, false));
        } else if ((f.control & 0x0F) == 0x01) {  // RR → следующий сегмент
            rrs++;
            sendSegment();
        } else if ((f.control & 1) == 0) {  // I-кадр
            nr_ = (uint8_t)(((f.control >> 1) + 1) & 7);
            if (f.info.size() < 3 || f.info[0] != 0xE6 || f.info[1] != 0xE6 || f.info[2] != 0x00) {
                badFrames++;
                return;
            }
            Bytes pdu(f.info.begin() + 3, f.info.end());
            Bytes info{0xE6, 0xE7, 0x00};
            Bytes resp = handle(pdu);
            info.insert(info.end(), resp.begin(), resp.end());
            segments_.clear();
            for (size_t i = 0; i < info.size(); i += maxInfo)
                segments_.emplace_back(info.begin() + i, info.begin() + std::min(info.size(), i + maxInfo));
            sendSegment();
        }
    }

    Bytes handle(const Bytes& pdu) {
        if (!pdu.empty() && pdu[0] == 0x60) return aare(pdu);
        if (pdu.size() >= 13 && pdu[0] == 0xC0 && pdu[1] == 0x01) {
            gets++;
            char obis[32];
            snprintf(obis, sizeof(obis), "%u.%u.%u.%u.%u.%u", pdu[5], pdu[6], pdu[7], pdu[8], pdu[9], pdu[10]);
            auto it = objects_.find(key((uint16_t)((pdu[3] << 8) | pdu[4]), obis, pdu[11]));
            Bytes r{0xC4, 0x01, pdu[2]};
            if (it == objects_.end()) {
                r.push_back(0x01);  // data-access-result
                r.push_back(0x04);  // object-undefined
            } else {
                r.push_back(0x00);
                r.insert(r.end(), it->second.begin(), it->second.end());
            }
            return r;
        }
        return hex("d8 01 01");  // exception-response
    }

    Bytes aare(const Bytes& aarq) {
        aarqs++;
        std::string pwd;
        for (size_t i = 2; i + 1 < aarq.size(); i += 2 + aarq[i + 1]) {
            if (aarq[i] == 0xAC && i + 3 < aarq.size())  // calling-authentication-value
                pwd.assign(aarq.begin() + i + 4, aarq.begin() + i + 4 + aarq[i + 3]);
        }
        if (pwd == password)
            return hex("61 29 a1 09 06 07 60 85 74 05 08 01 01 a2 03 02 01 00 a3 05 a1 03 02 01 00"
                       " be 10 04 0e 08 00 06 5f 1f 04 00 00 10 1d 03 e8 00 07");
        // rejected-permanent, authentication-failure
        return hex("61 17 a1 09 06 07 60 85 74 05 08 01 01 a2 03 02 01 01 a3 05 a1 03 02 01 0d");
    }

    void sendSegment() {
        if (segments_.empty()) return;
        Bytes seg = segments_.front();
        segments_.pop_front();
        uint8_t control = (uint8_t)((nr_ << 5) | 0x10 | (ns_ << 1));
        ns_ = (uint8_t)((ns_ + 1) & 7);
        reply(serverFrame(addr_, control, seg, !segments_.empty()));
    }

    void reply(Bytes frame) {
        if (corruptFrames > 0) {
            corruptFrames--;
            frame[frame.size() - 2] ^= 0xFF;
        }
        send(frame);
    }

    const Bytes UA_PARAMS = hex("81 80 14 05 02 01 00 06 02 01 00 07 04 00 00 00 01 08 04 00 00 00 01");
    std::map<std::string, Bytes> objects_;
    std::deque<Bytes> segments_;
    FrameSplitter splitter_;
    uint8_t addr_ = 16;
    uint8_t ns_ = 0;
    uint8_t nr_ = 0;
};

// Воспроизводит реальный обмен: на запрос отвечает данными, которые прислал
// настоящий счётчик на такой же запрос. Кадры без данных (SNRM, DISC) должны
// совпасть целиком, I-кадры — по данным (LLC + PDU): номера последовательности
// в прошивке другие, потому что порядок чтения не как в скрипте. Ответ поэтому
// собирается заново, с номерами под запрос.
class RealExchangePort : public ByteQueue {
   public:
    std::vector<std::string> unmatched;  // запросы, которых нет в дампе

    size_t write(const uint8_t* d, size_t n) override {
        for (const Bytes& b : splitter_.push(d, n)) onFrame(b);
        return n;
    }

   private:
    void onFrame(const Bytes& raw) {
        Frame f = parseFrame(raw);
        for (const Exchange& e : REAL_EXCHANGE) {
            Bytes req = hex(e.request);
            bool same = f.info.empty() ? req == raw : (f.ok && parseFrame(req).info == f.info);
            if (!same) continue;
            Frame resp = parseFrame(hex(e.response));
            if (f.info.empty()) {  // SNRM, DISC — ответ как есть
                send(hex(e.response));
            } else {
                uint8_t ns = (uint8_t)((f.control >> 1) & 7);
                uint8_t control = (uint8_t)((((ns + 1) & 7) << 5) | 0x10 | (ns << 1));
                send(serverFrame(f.dst[1] >> 1, control, resp.info, false));
            }
            return;
        }
        unmatched.push_back(toHex(raw));
    }

    FrameSplitter splitter_;
};
