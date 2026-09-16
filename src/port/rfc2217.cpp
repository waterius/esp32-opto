#include "rfc2217.h"

#include <WiFi.h>

#include "../app.h"
#include "opto_esp32.h"

namespace rfc2217 {
namespace {

// telnet
const uint8_t IAC = 255, SE = 240, SB = 250, WILL = 251, WONT = 252, DO = 253, DONT = 254;
const uint8_t OPT_BINARY = 0, OPT_SGA = 3, OPT_COMPORT = 44;
// RFC 2217, команды клиента
const uint8_t SET_BAUDRATE = 1, SET_DATASIZE = 2, SET_PARITY = 3, SET_STOPSIZE = 4;
const uint8_t SERVER_OFFSET = 100;  // ответ сервера = команда + 100

WiFiServer* server = nullptr;
WiFiClient client;
bool active = false;

enum class St { Data, Iac, Opt, Sub, SubIac };
St st = St::Data;
uint8_t negotiate = 0;
uint8_t sub[16];
uint8_t subLen = 0;

bool supported(uint8_t opt) { return opt == OPT_BINARY || opt == OPT_SGA || opt == OPT_COMPORT; }

void sendCmd(uint8_t verb, uint8_t opt) {
    uint8_t b[3] = {IAC, verb, opt};
    client.write(b, 3);
}

void sendSub(const uint8_t* payload, size_t len) {
    uint8_t head[3] = {IAC, SB, OPT_COMPORT};
    client.write(head, 3);
    for (size_t i = 0; i < len; i++) {
        if (payload[i] == IAC) client.write(IAC);
        client.write(payload[i]);
    }
    uint8_t tail[2] = {IAC, SE};
    client.write(tail, 2);
}

// Текущие параметры порта → ответ клиенту
void replyBaud(uint32_t baud) {
    uint8_t p[5] = {(uint8_t)(SET_BAUDRATE + SERVER_OFFSET), (uint8_t)(baud >> 24),
                    (uint8_t)(baud >> 16), (uint8_t)(baud >> 8), (uint8_t)baud};
    sendSub(p, 5);
}

void replyByte(uint8_t cmd, uint8_t value) {
    uint8_t p[2] = {(uint8_t)(cmd + SERVER_OFFSET), value};
    sendSub(p, 2);
}

uint8_t parityCode(char p) { return p == 'O' ? 2 : (p == 'E' ? 3 : 1); }
char parityChar(uint8_t c) { return c == 2 ? 'O' : (c == 3 ? 'E' : 'N'); }

void applySub() {
    if (subLen < 1) return;
    core::SerialCfg cfg = opto.current();
    switch (sub[0]) {
        case SET_BAUDRATE: {
            if (subLen < 5) return;
            uint32_t baud = ((uint32_t)sub[1] << 24) | ((uint32_t)sub[2] << 16) |
                            ((uint32_t)sub[3] << 8) | sub[4];
            if (baud) cfg.baud = baud;  // 0 = «просто скажи текущую»
            opto.configure(cfg);
            replyBaud(cfg.baud);
            break;
        }
        case SET_DATASIZE:
            if (subLen < 2) return;
            if (sub[1] >= 5 && sub[1] <= 8) cfg.bits = sub[1];
            opto.configure(cfg);
            replyByte(SET_DATASIZE, cfg.bits);
            break;
        case SET_PARITY:
            if (subLen < 2) return;
            if (sub[1] >= 1 && sub[1] <= 3) cfg.parity = parityChar(sub[1]);
            opto.configure(cfg);
            replyByte(SET_PARITY, parityCode(cfg.parity));
            break;
        case SET_STOPSIZE:
            if (subLen < 2) return;
            if (sub[1] == 1 || sub[1] == 2) cfg.stop = sub[1];
            opto.configure(cfg);
            replyByte(SET_STOPSIZE, cfg.stop);
            break;
        default:
            break;  // остальные команды (control, flow) молча игнорируем
    }
}

void onConnect() {
    active = true;
    st = St::Data;
    subLen = 0;
    sendCmd(WILL, OPT_BINARY);
    sendCmd(DO, OPT_BINARY);
    sendCmd(WILL, OPT_SGA);
    sendCmd(DO, OPT_SGA);
    sendCmd(DO, OPT_COMPORT);
}

void onDisconnect() {
    active = false;
    client.stop();
    applySerialCfg();  // клиент мог поменять скорость — вернуть настройки счётчика
}

// Байты от клиента: telnet-разбор, данные — в оптопорт
void fromClient() {
    while (client.available()) {
        uint8_t c = (uint8_t)client.read();
        switch (st) {
            case St::Data:
                if (c == IAC)
                    st = St::Iac;
                else
                    opto.write(&c, 1);
                break;
            case St::Iac:
                if (c == IAC) {  // экранированный 0xFF — это данные
                    opto.write(&c, 1);
                    st = St::Data;
                } else if (c == SB) {
                    subLen = 0;
                    st = St::Sub;
                } else if (c == WILL || c == WONT || c == DO || c == DONT) {
                    negotiate = c;
                    st = St::Opt;
                } else {
                    st = St::Data;
                }
                break;
            case St::Opt:
                if (negotiate == DO)
                    sendCmd(supported(c) ? WILL : WONT, c);
                else if (negotiate == WILL)
                    sendCmd(supported(c) ? DO : DONT, c);
                st = St::Data;
                break;
            case St::Sub:
                if (c == IAC)
                    st = St::SubIac;
                else if (subLen < sizeof(sub))
                    sub[subLen++] = c;
                break;
            case St::SubIac:
                if (c == SE) {
                    // sub[0] = номер опции, дальше команда
                    if (subLen >= 2 && sub[0] == OPT_COMPORT) {
                        memmove(sub, sub + 1, --subLen);
                        applySub();
                    }
                    subLen = 0;
                    st = St::Data;
                } else {
                    if (subLen < sizeof(sub)) sub[subLen++] = c;
                    st = St::Sub;
                }
                break;
        }
    }
}

// Байты из оптопорта → клиенту, 0xFF экранируем
void toClient() {
    uint8_t buf[128];
    size_t n = 0;
    while (opto.available() && n < sizeof(buf) - 1) {
        int c = opto.read();
        if (c < 0) break;
        buf[n++] = (uint8_t)c;
        if ((uint8_t)c == IAC) buf[n++] = IAC;
    }
    if (n) client.write(buf, n);
}

}  // namespace

void begin(uint16_t port) {
    if (server) {
        server->stop();
        delete server;
    }
    server = new WiFiServer(port);
    server->begin();
    server->setNoDelay(true);
}

void loop() {
    if (!server) return;
    if (!active) {
        WiFiClient c = server->available();
        if (c) {
            client = c;
            client.setNoDelay(true);
            onConnect();
        }
        return;
    }
    if (!client.connected()) {
        onDisconnect();
        return;
    }
    fromClient();
    toClient();
}

bool busy() { return active; }

}  // namespace rfc2217
