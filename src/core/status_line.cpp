#include "status_line.h"

#include <stdio.h>
#include <string.h>

namespace core {
namespace {

// Пишет в буфер и никогда за него не выходит: всё, что не влезло, отбрасывается.
struct Writer {
    char* out;
    size_t cap;  // вместе с местом под '\0'
    size_t pos;

    Writer(char* buf, size_t size) : out(buf), cap(size), pos(0) {}

    bool room(size_t n) const { return pos + n + 1 <= cap; }
    void ch(char c) {
        if (room(1)) out[pos++] = c;
    }
    void raw(const char* text) {
        while (*text && room(1)) out[pos++] = *text++;
    }
};

void key(Writer& w, const char* name) {
    w.ch(' ');
    w.raw(name);
    w.ch('=');
}

void u32(Writer& w, uint32_t value) {
    char tmp[12];
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)value);
    w.raw(tmp);
}

void i32(Writer& w, int value) {
    char tmp[12];
    snprintf(tmp, sizeof(tmp), "%d", value);
    w.raw(tmp);
}

// Длина символа UTF-8 по первому байту. Байт-продолжение без начала пропускаем
// по одному: чинить испорченный текст не наша задача, а зациклиться нельзя.
size_t charLen(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

// Текстовое поле: всегда в кавычках, даже пустое, — тогда вся строка разбирается
// одним регекспом. Символы добавляются целиком, поэтому нехватка места обрежет
// поле по границе буквы, а не посреди неё, и закрывающая кавычка будет всегда.
void quoted(Writer& w, const char* text, size_t limit) {
    if (!text) text = "";
    size_t len = strlen(text);
    if (len > limit) len = limit;
    if (!w.room(2)) return;
    w.ch('"');
    size_t i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)text[i];
        size_t n = charLen(c);
        if (i + n > len) break;  // оборванный хвост
        bool escaped = n == 1 && (c == '"' || c == '\\');
        if (!w.room(n + (escaped ? 1 : 0) + 1)) break;  // +1 — закрывающая кавычка
        if (n == 1) {
            if (escaped) w.ch('\\');
            // Перевод строки внутри значения разорвал бы строку состояния надвое
            w.ch(c < 0x20 ? ' ' : (char)c);
        } else {
            for (size_t k = 0; k < n; ++k) w.ch(text[i + k]);
        }
        i += n;
    }
    w.ch('"');
}

}  // namespace

const char* wifiModeName(WifiMode mode) {
    switch (mode) {
        case WifiMode::Down: return "off";
        case WifiMode::Connecting: return "search";
        case WifiMode::Station: return "sta";
        case WifiMode::Ap: return "ap";
        case WifiMode::ApStation: return "ap+sta";
    }
    return "?";  // значение из будущей версии прошивки
}

size_t formatStatus(const StatusFacts& facts, char* out, size_t cap) {
    if (!out || !cap) return 0;
    Writer w{out, cap};
    char tmp[24];

    w.raw("status");

    key(w, "fw");
    w.raw(facts.version && facts.version[0] ? facts.version : "-");
    key(w, "up");
    u32(w, facts.uptimeS);
    key(w, "heap");
    u32(w, facts.heap);
    key(w, "boot");
    quoted(w, facts.bootReason, STATUS_BOOT_CAP);
    key(w, "safe");
    w.ch(facts.safeMode ? '1' : '0');

    key(w, "wifi");
    w.raw(wifiModeName(facts.wifi));
    // Имя раздаваемой точки: в режиме ap именно его ищут в списке сетей,
    // и путать его с сетью, к которой подключаемся, нельзя
    key(w, "ap");
    quoted(w, facts.apName, STATUS_AP_CAP);
    key(w, "apch");
    u32(w, facts.apChannel);
    key(w, "apcfg");
    u32(w, facts.apCfgChannel);
    key(w, "apcli");
    u32(w, facts.apClients);
    key(w, "ssid");
    quoted(w, facts.ssid, STATUS_SSID_CAP);
    key(w, "ip");
    // Пустое значение сдвинуло бы поля у наивного разбора по пробелам
    w.raw(facts.ip && facts.ip[0] ? facts.ip : "-");
    key(w, "rssi");
    i32(w, facts.rssi);
    key(w, "drops");
    u32(w, facts.drops);
    key(w, "offline");
    u32(w, facts.offlineS);

    key(w, "bus");
    w.raw(facts.busOwner && facts.busOwner[0] ? facts.busOwner : "-");
    key(w, "port");
    snprintf(tmp, sizeof(tmp), "%lu-%u%c%u", (unsigned long)facts.port.baud, facts.port.bits,
             facts.port.parity, facts.port.stop);
    w.raw(tmp);
    key(w, "rfc");
    w.ch(facts.rfcClient ? '1' : '0');

    key(w, "read");
    w.ch(facts.hasReading ? '1' : '0');
    key(w, "total");
    snprintf(tmp, sizeof(tmp), "%.3f", facts.total);
    w.raw(tmp);
    key(w, "merr");
    quoted(w, facts.meterError, STATUS_METER_ERROR_CAP);
    key(w, "mnext");
    u32(w, facts.nextReadS);

    key(w, "code");
    i32(w, facts.cloudCode);
    key(w, "cerr");
    quoted(w, facts.cloudError, STATUS_CLOUD_ERROR_CAP);
    key(w, "cnext");
    u32(w, facts.nextSendS);

    out[w.pos] = 0;
    return w.pos;
}

}  // namespace core
