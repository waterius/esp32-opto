#include "nartis.h"

#include <stdio.h>
#include <string.h>

namespace core {
namespace {

const uint8_t CLIENT_READER = 32;  // считыватель показаний

// OBIS
const uint8_t OBIS_SERIAL[6] = {0, 0, 96, 1, 0, 255};
const uint8_t OBIS_MODEL[6] = {0, 0, 96, 1, 1, 255};
const uint8_t OBIS_FW[6] = {0, 0, 96, 1, 2, 255};
const uint8_t OBIS_CLOCK[6] = {0, 0, 1, 0, 0, 255};

// 1.0.1.8.t.255 — активная энергия импорт: t = 0 сумма, 1..4 тарифы
void obisEnergy(uint8_t t, uint8_t out[6]) {
    out[0] = 1;
    out[1] = 0;
    out[2] = 1;
    out[3] = 8;
    out[4] = t;
    out[5] = 255;
}

const uint16_t CLASS_DATA = 1;
const uint16_t CLASS_REGISTER = 3;
const uint16_t CLASS_CLOCK = 8;
const uint8_t UNIT_WH = 30;

}  // namespace

void NartisMeter::setPassword(const char* pwd) {
    snprintf(pwd_, sizeof(pwd_), "%s", pwd ? pwd : "");
}

bool NartisMeter::readRegister(const uint8_t obis[6], double& kwh) {
    uint8_t buf[64];
    size_t n = 0;
    double raw = 0;
    if (!dlms_.get(CLASS_REGISTER, obis, 2, buf, sizeof(buf), n)) return false;
    if (!DlmsClient::parseNumber(buf, n, raw)) return false;

    int8_t scaler = 0;
    uint8_t unit = UNIT_WH;
    if (dlms_.get(CLASS_REGISTER, obis, 3, buf, sizeof(buf), n))
        DlmsClient::parseScalerUnit(buf, n, scaler, unit);

    double v = raw;
    for (int i = 0; i < scaler; i++) v *= 10.0;
    for (int i = 0; i > scaler; i--) v /= 10.0;
    if (unit == UNIT_WH) v /= 1000.0;  // Вт·ч → кВт·ч
    kwh = v;
    return true;
}

void NartisMeter::readString(const uint8_t obis[6], char* out, size_t cap) {
    uint8_t buf[64];
    size_t n = 0;
    if (dlms_.get(CLASS_DATA, obis, 2, buf, sizeof(buf), n))
        DlmsClient::parseString(buf, n, out, cap);
}

bool NartisMeter::readAll(MeterData& out) {
    uint8_t obis[6];
    obisEnergy(0, obis);
    if (!readRegister(obis, out.total)) {
        snprintf(out.error, sizeof(out.error), "энергия: %s", dlms_.lastError());
        return false;
    }
    out.tariffCount = 0;
    for (uint8_t t = 1; t <= MAX_TARIFFS; t++) {
        obisEnergy(t, obis);
        double v = 0;
        if (!readRegister(obis, v)) break;
        out.tariff[t - 1] = v;
        out.tariffCount = t;
    }
    readString(OBIS_SERIAL, out.serial, sizeof(out.serial));
    readString(OBIS_MODEL, out.model, sizeof(out.model));
    readString(OBIS_FW, out.fwVersion, sizeof(out.fwVersion));

    uint8_t buf[64];
    size_t n = 0;
    if (dlms_.get(CLASS_CLOCK, OBIS_CLOCK, 2, buf, sizeof(buf), n))
        DlmsClient::parseDateTime(buf, n, out.time, sizeof(out.time));

    out.valid = true;
    return true;
}

bool NartisMeter::read(MeterData& out) {
    out = MeterData();
    // Порядок перебора: сначала найденный ранее адрес, потом типовые.
    uint8_t candidates[3];
    uint8_t count = 0;
    if (addr_) {
        candidates[count++] = addr_;
    } else {
        if (found_) candidates[count++] = found_;
        if (found_ != 16) candidates[count++] = 16;  // НАРТИС-100/300
        if (found_ != 17) candidates[count++] = 17;  // НАРТИС-И100/И300
    }

    for (uint8_t i = 0; i < count; i++) {
        if (!dlms_.connect(candidates[i], CLIENT_READER, pwd_)) {
            snprintf(out.error, sizeof(out.error), "адрес %u: %s", candidates[i], dlms_.lastError());
            sleepMs(300);
            continue;
        }
        found_ = candidates[i];
        bool ok = readAll(out);
        dlms_.disconnect();
        return ok;
    }
    return false;
}

}  // namespace core
