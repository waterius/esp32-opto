#include "nartis.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "client.h"
#include "cosem.h"
#include "gxobjects.h"
#include "variant.h"

namespace core {
namespace {

const uint16_t CLIENT_READER = 32;  // считыватель показаний
const uint32_t WAIT_MS = 2000;      // как в примере Gurux Arduino_IDE/client
const uint8_t RESEND_COUNT = 3;
const unsigned char HDLC_FLAG = 0x7E;
const unsigned char UNIT_WH = 30;

// Один сеанс со счётчиком: настройки Gurux и буфер приёма.
// Порядок обмена — как в Arduino_IDE/client/client.ino из GuruxDLMS.c.
class Session {
   public:
    Session(IOptoPort& port, uint8_t phys, const char* pwd) : port_(port) {
        cl_init(&settings_, 1, CLIENT_READER, cl_getServerAddress(1, phys, 2),
                pwd[0] ? DLMS_AUTHENTICATION_LOW : DLMS_AUTHENTICATION_NONE, pwd[0] ? pwd : NULL,
                DLMS_INTERFACE_TYPE_HDLC);
        // Набор услуг и размер PDU в AARQ — как в обмене, проверенном на
        // счётчике (docs/06-nartis-100-exchange.md, conformance 00 7e 1f,
        // max PDU 1200). Свои умолчания Gurux предлагает шире.
        settings_.proposedConformance = (DLMS_CONFORMANCE)(
            DLMS_CONFORMANCE_PRIORITY_MGMT_SUPPORTED | DLMS_CONFORMANCE_ATTRIBUTE_0_SUPPORTED_WITH_GET |
            DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_GET_OR_READ | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_SET_OR_WRITE |
            DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_ACTION | DLMS_CONFORMANCE_MULTIPLE_REFERENCES |
            DLMS_CONFORMANCE_GET | DLMS_CONFORMANCE_SET | DLMS_CONFORMANCE_SELECTIVE_ACCESS |
            DLMS_CONFORMANCE_EVENT_NOTIFICATION | DLMS_CONFORMANCE_ACTION);
        settings_.maxPduSize = 1200;
        BYTE_BUFFER_INIT(&frame_);
        bb_capacity(&frame_, 256);
    }

    ~Session() {
        bb_clear(&frame_);
        cl_clear(&settings_);
    }

    // SNRM → UA → AARQ → AARE. Возвращает код Gurux, 0 — успех.
    int open() {
        message msg;
        gxReplyData reply;
        mes_init(&msg);
        reply_init(&reply);
        int ret = cl_snrmRequest(&settings_, &msg);
        if (ret == 0) ret = exchange(&msg, &reply);
        if (ret == 0) ret = cl_parseUAResponse(&settings_, &reply.data);
        mes_clear(&msg);
        reply_clear(&reply);
        if (ret != 0) return ret;

        ret = cl_aarqRequest(&settings_, &msg);
        if (ret == 0) ret = exchange(&msg, &reply);
        if (ret == 0) ret = cl_parseAAREResponse(&settings_, &reply.data);
        mes_clear(&msg);
        reply_clear(&reply);
        // Счётчик ответил на AARQ отказом: пароль неверный либо не принят
        // механизм аутентификации. Кодов у Gurux несколько (для LLS обычно
        // DLMS_ERROR_CODE_AUTHENTICATION_FAILURE), поэтому отказом считаем
        // любой ответ, кроме молчания.
        refused_ = ret != 0 && !aborted_ && ret != DLMS_ERROR_CODE_RECEIVE_FAILED;
        if (ret == 0) resend_ = true;  // повторы разрешены только после ассоциации
        return ret;
    }

    // Счётчик отверг ассоциацию: повторять нельзя, 5 попыток — блокировка на сутки.
    bool refused() const { return refused_; }

    // DISC. Ошибки не важны: счётчик сам закроет сеанс по таймауту.
    // RLRQ (release) не шлём: проверенный на счётчике обмен закрывается одним
    // DISC (docs/06-nartis-100-exchange.md), а лишний запрос — лишние 2 секунды
    // ожидания, если счётчик на него не отвечает.
    void close() {
        message msg;
        gxReplyData reply;
        mes_init(&msg);
        reply_init(&reply);
        if (cl_disconnectRequest(&settings_, &msg) == 0) exchange(&msg, &reply);
        mes_clear(&msg);
        reply_clear(&reply);
    }

    // GET атрибута. update — разобрать значение в объект через cl_updateValue.
    // Для строк update не используется: у Gurux там утечки (так же обходит latonita).
    int readAttr(gxObject* obj, unsigned char attr, gxReplyData* reply, bool update) {
        message msg;
        mes_init(&msg);
        int ret = cl_read(&settings_, obj, attr, &msg);
        if (ret == 0) ret = exchange(&msg, reply);
        if (ret == 0 && update) ret = cl_updateValue(&settings_, obj, attr, &reply->dataValue);
        mes_clear(&msg);
        return ret;
    }

    bool aborted() const { return aborted_; }

   private:
    // Отправить все кадры сообщения и собрать ответ вместе с сегментами.
    int exchange(message* msg, gxReplyData* reply) {
        for (int i = 0; i < msg->size; ++i) {
            int ret = sendAndReceive(msg->data[i], reply);
            if (ret != 0) return ret;
            while (reply_isMoreData(reply)) {
                gxByteBuffer rr;
                BYTE_BUFFER_INIT(&rr);
                ret = cl_receiverReady(&settings_, reply->moreData, &rr);
                if (ret == 0) ret = sendAndReceive(&rr, reply);
                bb_clear(&rr);
                if (ret != 0) return ret;
            }
        }
        return 0;
    }

    int sendAndReceive(gxByteBuffer* data, gxReplyData* reply) {
        reply->complete = 0;
        bb_empty(&frame_);
        port_.flushInput();
        port_.write(data->data, data->size);
        uint8_t resend = 0;
        do {
            int ret = readFrame();
            if (ret != 0) {
                // До ассоциации повторов нет: повторный AARQ — ещё одна попытка
                // пароля, а повторный SNRM затягивает перебор адресов.
                if (aborted_ || !resend_ || resend == RESEND_COUNT) return ret;
                ++resend;
                bb_empty(&frame_);
                port_.write(data->data, data->size);
                continue;
            }
            ret = cl_getData(&settings_, &frame_, reply);
            if (ret != 0 && ret != DLMS_ERROR_CODE_FALSE) return ret;
        } while (reply->complete == 0);
        return 0;
    }

    // Дочитать байты до флага 0x7E в конце кадра (com_readSerialPort в примере Gurux).
    int readFrame() {
        uint32_t start = nowMs();
        uint32_t lastIndex = frame_.position;
        while (true) {
            if (port_.abortRequested()) {
                aborted_ = true;
                return DLMS_ERROR_CODE_RECEIVE_FAILED;
            }
            int avail = port_.available();
            if (avail > 0) {
                if (frame_.size + avail > frame_.capacity) bb_capacity(&frame_, 20 + frame_.size + avail);
                for (int i = 0; i < avail; ++i) {
                    int c = port_.read();
                    if (c < 0) break;
                    frame_.data[frame_.size++] = (unsigned char)c;
                }
                if (frame_.size > 5) {
                    for (uint32_t pos = frame_.size - 1; pos != lastIndex; --pos) {
                        if (frame_.data[pos] == HDLC_FLAG) return 0;
                    }
                    lastIndex = frame_.size - 1;
                }
            } else {
                sleepMs(1);  // флаг прерывания проверяется каждую миллисекунду
            }
            if (nowMs() - start >= WAIT_MS) return DLMS_ERROR_CODE_RECEIVE_FAILED;
        }
    }

    IOptoPort& port_;
    dlmsSettings settings_;
    gxByteBuffer frame_;
    bool aborted_ = false;
    bool refused_ = false;
    bool resend_ = false;
};

// Строки НАРТИС (тип счётчика) приходят в cp1251 — переводим в UTF-8 для веба и облака.
void cp1251ToUtf8(const unsigned char* src, size_t len, char* out, size_t cap) {
    size_t k = 0;
    for (size_t i = 0; i < len && src[i]; ++i) {
        unsigned char c = src[i];
        uint16_t u;
        if (c < 0x80) u = c < 0x20 ? '?' : c;
        else if (c >= 0xC0) u = 0x0410 + (c - 0xC0);  // А..я
        else if (c == 0xA8) u = 0x0401;               // Ё
        else if (c == 0xB8) u = 0x0451;               // ё
        else u = '?';
        if (u < 0x80) {
            if (k + 1 >= cap) break;
            out[k++] = (char)u;
        } else {
            if (k + 2 >= cap) break;
            out[k++] = (char)(0xC0 | (u >> 6));
            out[k++] = (char)(0x80 | (u & 0x3F));
        }
    }
    out[k] = 0;
}

// 1.0.1.8.t.255: атрибут 3 — scaler и unit, атрибут 2 — значение как есть.
// Gurux scaler не применяет (cosem_setRegister копирует значение).
int readEnergy(Session& s, uint8_t tariff, double& kwh) {
    char obis[20];
    snprintf(obis, sizeof(obis), "1.0.1.8.%u.255", tariff);
    gxRegister reg;
    gxReplyData reply;
    reply_init(&reply);
    int ret = cosem_init(BASE(reg), DLMS_OBJECT_TYPE_REGISTER, obis);
    if (ret == 0) ret = s.readAttr(BASE(reg), 3, &reply, true);
    reply_clear(&reply);
    if (ret == 0) ret = s.readAttr(BASE(reg), 2, &reply, true);
    if (ret == 0) {
        double v = var_toDouble(&reg.value);
        for (int i = 0; i < reg.scaler; ++i) v *= 10.0;
        for (int i = 0; i > reg.scaler; --i) v /= 10.0;
        if (reg.unit == UNIT_WH) v /= 1000.0;  // Вт·ч → кВт·ч
        kwh = v;
    }
    reply_clear(&reply);
    obj_clear(BASE(reg));
    return ret;
}

void readString(Session& s, const char* obis, char* out, size_t cap) {
    out[0] = 0;
    gxData obj;
    gxReplyData reply;
    reply_init(&reply);
    if (cosem_init(BASE(obj), DLMS_OBJECT_TYPE_DATA, obis) == 0 &&
        s.readAttr(BASE(obj), 2, &reply, false) == 0) {
        gxByteBuffer* bb = NULL;
        if (reply.dataValue.vt == DLMS_DATA_TYPE_OCTET_STRING) bb = reply.dataValue.byteArr;
        if (reply.dataValue.vt == DLMS_DATA_TYPE_STRING) bb = reply.dataValue.strVal;
        if (bb) cp1251ToUtf8(bb->data, bb->size, out, cap);
    }
    reply_clear(&reply);
    obj_clear(BASE(obj));
}

void readClock(Session& s, char* out, size_t cap) {
    out[0] = 0;
    gxClock clk;
    gxReplyData reply;
    reply_init(&reply);
    if (cosem_init(BASE(clk), DLMS_OBJECT_TYPE_CLOCK, "0.0.1.0.0.255") == 0 &&
        s.readAttr(BASE(clk), 2, &reply, true) == 0) {
        // Gurux отдаёт время счётчика как есть, без пересчёта по deviation,
        // поэтому и разбирать его надо без часового пояса устройства.
        time_t t = (time_t)clk.time.value;
        struct tm tm;
        gmtime_r(&t, &tm);
        strftime(out, cap, "%Y-%m-%d %H:%M:%S", &tm);
    }
    reply_clear(&reply);
    obj_clear(BASE(clk));
}

}  // namespace

void NartisMeter::setPassword(const char* pwd) {
    snprintf(pwd_, sizeof(pwd_), "%s", pwd ? pwd : "");
}

ReadResult NartisMeter::read(MeterData& out, char* error, size_t errorCap) {
    out = MeterData();
    error[0] = 0;

    uint8_t candidates[3];
    uint8_t count = 0;
    if (addr_) {
        candidates[count++] = addr_;
    } else {
        if (found_) candidates[count++] = found_;
        if (found_ != 16) candidates[count++] = 16;  // НАРТИС-100/300
        if (found_ != 17) candidates[count++] = 17;  // НАРТИС-И100/И300
    }

    for (uint8_t i = 0; i < count; ++i) {
        uint8_t addr = candidates[i];
        Session s(port_, addr, pwd_);
        int ret = s.open();
        if (s.aborted()) return ReadResult::Aborted;
        if (s.refused()) {
            snprintf(error, errorCap, "счётчик отверг пароль (адрес %u, код %d)", addr, ret);
            return ReadResult::AuthRejected;
        }
        if (ret != 0) {
            // На чужой адрес счётчик не отвечает — пробуем следующий
            snprintf(error, errorCap, "нет связи со счётчиком (адрес %u, код %d)", addr, ret);
            continue;
        }
        found_ = addr;

        ret = readEnergy(s, 0, out.total);
        if (ret == 0) {
            for (uint8_t t = 1; t <= MAX_TARIFFS && !s.aborted(); ++t) {
                double v = 0;
                if (readEnergy(s, t, v) != 0) break;  // тарифы читаются до первого отказа
                out.tariff[t - 1] = v;
                out.tariffCount = t;
            }
            readString(s, "0.0.96.1.0.255", out.serial, sizeof(out.serial));
            readString(s, "0.0.96.1.1.255", out.model, sizeof(out.model));
            readString(s, "0.0.96.1.2.255", out.fwVersion, sizeof(out.fwVersion));
            readClock(s, out.time, sizeof(out.time));
        }
        if (s.aborted()) return ReadResult::Aborted;  // порт уже у клиента — DISC не шлём
        s.close();
        if (ret != 0) {
            snprintf(error, errorCap, "ошибка чтения энергии (код %d)", ret);
            return ReadResult::Failed;
        }
        return ReadResult::Ok;
    }
    return ReadResult::Failed;
}

}  // namespace core
