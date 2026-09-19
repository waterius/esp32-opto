#!/usr/bin/env python3
"""Проверка связи с НАРТИС-100 через оптоголовку на USB.

DLMS/COSEM поверх HDLC, клиент 32, пароль LLS. Логика повторяет
src/core/dlms.cpp: SNRM на 9600 8N1 по адресам 16 и 17, при тишине —
режим E (/?! на 300 7E1). Пароль отправляется РОВНО ОДИН раз за запуск:
после 5 неверных паролей счётчик блокирует интерфейсы на сутки.

    python3 tools/nartis_probe.py                    # порт найдётся сам, пароль 111
    python3 tools/nartis_probe.py -p 111 -v          # с дампом кадров
    python3 tools/nartis_probe.py --link-only        # только SNRM/UA, без пароля
    python3 tools/nartis_probe.py --port rfc2217://192.168.50.95:2217   # через прошивку
"""
import argparse
import datetime
import glob
import struct
import sys
import time

import serial

LLC_REQ = b"\xE6\xE6\x00"
LLC_RSP = b"\xE6\xE7\x00"
VERBOSE = False


def log(direction, data):
    if VERBOSE:
        print(f"  {direction} {data.hex(' ')}")


def fcs16(data):
    fcs = 0xFFFF
    for b in data:
        fcs ^= b
        for _ in range(8):
            fcs = (fcs >> 1) ^ 0x8408 if fcs & 1 else fcs >> 1
    return fcs ^ 0xFFFF


class LinkError(Exception):
    pass


class Hdlc:
    def __init__(self, ser, phys, client):
        self.ser = ser
        self.dst = bytes([0x02, (phys << 1) | 1])
        self.src = bytes([(client << 1) | 1])
        self.ss = self.rs = 0
        self.last_sent = b""

    def send(self, control, info=b""):
        length = 2 + len(self.dst) + len(self.src) + 1 + 2 + (len(info) + 2 if info else 0)
        body = bytes([0xA0 | (length >> 8) & 7, length & 0xFF]) + self.dst + self.src + bytes([control])
        body += fcs16(body).to_bytes(2, "little")
        if info:
            body += info
            body += fcs16(body).to_bytes(2, "little")
        self.last_sent = body
        log(">>", b"\x7e" + body + b"\x7e")
        self.ser.write(b"\x7e" + body + b"\x7e")

    def _read(self, n, deadline):
        out = bytearray()
        while len(out) < n:
            if time.monotonic() > deadline:
                raise LinkError("нет ответа" if not out else "обрыв кадра")
            out += self.ser.read(n - len(out))
        return bytes(out)

    def recv(self, timeout=3.0):
        """Возвращает (control, info, segmented)."""
        deadline = time.monotonic() + timeout
        while True:
            while self._read(1, deadline) != b"\x7e":
                pass
            b0 = self._read(1, deadline)
            if b0 == b"\x7e":  # сдвоенный флаг
                b0 = self._read(1, deadline)
            b1 = self._read(1, deadline)
            length = ((b0[0] & 7) << 8) | b1[0]
            body = b0 + b1 + self._read(length - 2, deadline)
            if body == self.last_sent:  # эхо головки
                log("эхо", body)
                continue
            log("<<", b"\x7e" + body + b"\x7e")
            if fcs16(body[:-2]).to_bytes(2, "little") != body[-2:]:
                raise LinkError("FCS не сошёлся")
            i = 2
            while not body[i] & 1:  # адрес получателя
                i += 1
            i += 1
            while not body[i] & 1:  # адрес отправителя
                i += 1
            i += 1
            control = body[i]
            info = body[i + 3 : -2] if len(body) > i + 3 + 2 else b""
            return control, info, bool(b0[0] & 0x08)

    def snrm(self):
        self.ss = self.rs = 0
        self.send(0x93)
        control, _, _ = self.recv(2.0)
        if control & 0xEF != 0x63:
            raise LinkError(f"вместо UA пришёл control={control:02X}")

    def disc(self):
        try:
            self.send(0x53)
            self.recv(1.0)
        except LinkError:
            pass

    def request(self, pdu):
        self.send((self.rs << 5) | 0x10 | (self.ss << 1), LLC_REQ + pdu)
        self.ss = (self.ss + 1) & 7
        resp = b""
        first = True
        while True:
            control, info, segmented = self.recv()
            if control & 1 == 0:  # I-кадр
                self.rs = ((control >> 1) + 1) & 7
            if first:
                if not info.startswith(LLC_RSP):
                    raise LinkError("нет LLC в ответе")
                info = info[3:]
                first = False
            resp += info
            if not segmented:
                return resp
            self.send((self.rs << 5) | 0x11)  # RR, следующий сегмент


# --- A-XDR -----------------------------------------------------------------

def _len(d, i):
    n = d[i]
    if n < 0x80:
        return n, i + 1
    k = n & 0x7F
    return int.from_bytes(d[i + 1 : i + 1 + k], "big"), i + 1 + k


FIXED = {3: ">?", 5: ">i", 6: ">I", 15: ">b", 16: ">h", 17: ">B", 18: ">H",
         20: ">q", 21: ">Q", 22: ">B", 23: ">f", 24: ">d"}


def decode(d, i=0):
    t = d[i]
    i += 1
    if t == 0:
        return None, i
    if t in (1, 2):
        n, i = _len(d, i)
        items = []
        for _ in range(n):
            v, i = decode(d, i)
            items.append(v)
        return items, i
    if t in FIXED:
        fmt = FIXED[t]
        size = struct.calcsize(fmt)
        return struct.unpack(fmt, d[i : i + size])[0], i + size
    if t in (9, 10, 12):
        n, i = _len(d, i)
        return bytes(d[i : i + n]), i + n
    if t == 4:
        n, i = _len(d, i)
        return bytes(d[i : i + (n + 7) // 8]), i + (n + 7) // 8
    if t == 25:
        return bytes(d[i : i + 12]), i + 12
    raise ValueError(f"тип A-XDR {t} не разобран: {d[i-1:].hex(' ')}")


def fmt_datetime(b):
    if len(b) < 12:
        return f"? {b.hex(' ')}"
    year, mon, day, dow, h, m, s, hs, dev, status = struct.unpack(">HBBBBBBBhB", b[:12])
    tz = "" if dev == -32768 else f" (отклонение {dev} мин)"
    return f"{year:04}-{mon:02}-{day:02} {h:02}:{m:02}:{s:02}{tz}, статус {status:02X}"


def text(b):
    if all(0x20 <= c < 0x7F for c in b):
        return b.decode("ascii")
    try:
        return b.decode("cp1251") + f"  [{b.hex(' ')}]"
    except UnicodeDecodeError:
        return b.hex(" ")


# --- COSEM -----------------------------------------------------------------

AARE_RESULT = {0: "accepted", 1: "rejected-permanent", 2: "rejected-transient"}
AARE_DIAG = {0: "null", 1: "no-reason-given", 2: "application-context-name-not-supported",
             11: "authentication-mechanism-name-not-recognised",
             12: "authentication-mechanism-name-required", 13: "authentication-failure",
             14: "authentication-required"}
DATA_ERR = {1: "hardware-fault", 2: "temporary-failure", 3: "read-write-denied",
            4: "object-undefined", 9: "object-class-inconsistent", 11: "object-unavailable",
            12: "type-unmatched", 13: "scope-of-access-violated", 250: "other-reason"}


def aarq(password):
    body = bytes.fromhex("A109060760857405080101")
    if password:
        pw = password.encode()
        body += bytes.fromhex("8A0207808B0760857405080201")
        body += bytes([0xAC, len(pw) + 2, 0x80, len(pw)]) + pw
    body += bytes.fromhex("BE10040E01000000065F1F0400007E1F04B0")
    return bytes([0x60, len(body)]) + body


def parse_aare(resp):
    if not resp or resp[0] != 0x61:
        raise LinkError(f"вместо AARE пришло {resp.hex(' ')}")
    result = diag = None
    i = 2
    while i < len(resp):
        tag, n = resp[i], resp[i + 1]
        val = resp[i + 2 : i + 2 + n]
        if tag == 0xA2:  # result: 02 01 xx
            result = val[-1]
        elif tag == 0xA3:  # source-diagnostic: A1|A2 03 02 01 xx
            diag = (val[0], val[-1])
        i += 2 + n
    return result, diag


def obis(s):
    return bytes(int(x) for x in s.split("."))


def get(link, cls, name, attr):
    pdu = bytes([0xC0, 0x01, 0xC1]) + cls.to_bytes(2, "big") + obis(name) + bytes([attr, 0x00])
    resp = link.request(pdu)
    if resp[:2] == b"\xC4\x02":
        raise LinkError("ответ блоками (get-response-with-datablock) не поддержан")
    if resp[:2] != b"\xC4\x01":
        raise LinkError(f"не get-response: {resp.hex(' ')}")
    if resp[3] != 0:
        raise LinkError(f"ошибка доступа {resp[4]} {DATA_ERR.get(resp[4], '')}")
    return decode(resp, 4)[0]


# --- порт ------------------------------------------------------------------

def open_port(path):
    """Локальная головка или прошивка по RFC 2217: rfc2217://192.168.50.95:2217."""
    if "://" in path:
        return serial.serial_for_url(path, 9600, bytesize=8, parity="N", stopbits=1, timeout=0.05)
    return serial.Serial(path, 9600, bytesize=8, parity="N", stopbits=1, timeout=0.05)


def mode_e(ser):
    """IEC 62056-21 режим E: /?! на 300 7E1, ACK, переход на HDLC 8N1."""
    ser.baudrate, ser.bytesize, ser.parity = 300, 7, "E"
    ser.reset_input_buffer()
    ser.write(b"/?!\r\n")
    ser.flush()
    deadline = time.monotonic() + 3
    ident = b""
    while time.monotonic() < deadline and not ident.endswith(b"\n"):
        ident += ser.read(64)
    ident = ident.replace(b"/?!\r\n", b"")  # эхо
    if not ident.startswith(b"/"):
        raise LinkError(f"режим E: нет идентификации ({ident!r})")
    print(f"  идентификация: {ident.strip().decode(errors='replace')}")
    z = chr(ident[4])
    rates = {"0": 300, "1": 600, "2": 1200, "3": 2400, "4": 4800, "5": 9600, "6": 19200}
    ser.write(b"\x062" + z.encode() + b"2\r\n")
    ser.flush()
    time.sleep(0.3)
    ser.baudrate, ser.bytesize, ser.parity = rates.get(z, 9600), 8, "N"
    ser.reset_input_buffer()
    print(f"  переход на HDLC {ser.baudrate} 8N1")


def find_link(ser, client, addrs):
    for phys in addrs:
        link = Hdlc(ser, phys, client)
        try:
            link.snrm()
            print(f"  UA от физического адреса {phys}")
            return link
        except LinkError as e:
            print(f"  адрес {phys}: {e}")
    return None


def main():
    global VERBOSE
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="/dev/cu.usbserial-* (по умолчанию первый) "
                                   "или rfc2217://host:2217 — прозрачный serial прошивки")
    ap.add_argument("-p", "--password", default="111", help="пароль чтения (по РЭ НАРТИС-100: 111)")
    ap.add_argument("--client", type=int, default=32)
    ap.add_argument("--addr", type=int, help="физический адрес; по умолчанию 16, затем 17")
    ap.add_argument("--tariffs", type=int, default=4)
    ap.add_argument("--mode-e", action="store_true", help="сразу режим E, без попытки 9600")
    ap.add_argument("--link-only", action="store_true", help="только SNRM/UA, пароль не отправлять")
    ap.add_argument("--scan", action="store_true", help="перебрать объекты в поиске версии коммуникационного ПО")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()
    VERBOSE = args.verbose

    port = args.port or next(iter(sorted(glob.glob("/dev/cu.usbserial-*"))), None)
    if not port:
        sys.exit("оптоголовка не найдена, укажите --port")
    addrs = [args.addr] if args.addr else [16, 17]
    print(f"порт {port}, клиент {args.client}")

    ser = open_port(port)
    link = None
    try:
        if not args.mode_e:
            print("SNRM на 9600 8N1")
            link = find_link(ser, args.client, addrs)
        if not link:
            print("режим E")
            mode_e(ser)
            link = find_link(ser, args.client, addrs)
        if not link:
            sys.exit("счётчик не ответил: проверьте положение головки")
        if args.link_only:
            print("канал есть, пароль не отправлялся")
            return

        print(f"AARQ с паролем {args.password!r} (одна попытка)")
        result, diag = parse_aare(link.request(aarq(args.password)))
        if result != 0:
            d = f"{AARE_DIAG.get(diag[1], diag[1])}" if diag else "?"
            sys.exit(f"ассоциация отклонена: {AARE_RESULT.get(result, result)}, {d}\n"
                     "НЕ повторяйте подбор: после 5 неверных паролей блокировка на сутки")
        print("ассоциация установлена, пароль верный\n")

        def show(label, fn):
            try:
                print(f"{label:<34} {fn()}")
            except (LinkError, ValueError, IndexError, struct.error) as e:
                print(f"{label:<34} ОШИБКА: {e}")

        show("тип счётчика 0.0.96.1.1.255", lambda: text(get(link, 1, "0.0.96.1.1.255", 2)))
        show("серийный номер 0.0.96.1.0.255", lambda: text(get(link, 1, "0.0.96.1.0.255", 2)))
        show("ПО метрол. 0.0.96.1.2.255", lambda: text(get(link, 1, "0.0.96.1.2.255", 2)))
        show("ПО коммуник. 0.0.96.1.3.255", lambda: text(get(link, 1, "0.0.96.1.3.255", 2)))

        def clock():
            v = get(link, 8, "0.0.1.0.0.255", 2)
            return f"{fmt_datetime(v)}   (компьютер: {datetime.datetime.now():%Y-%m-%d %H:%M:%S})"

        show("время 0.0.1.0.0.255", clock)

        def energy(name):
            raw = get(link, 3, name, 2)
            scaler, unit = get(link, 3, name, 3)
            value = raw * 10 ** scaler
            if unit == 30:  # Wh
                return f"{value / 1000:.3f} кВт·ч   (raw={raw}, scaler={scaler}, unit=Wh)"
            return f"{value}   (raw={raw}, scaler={scaler}, unit={unit})"

        show("A+ сумма 1.0.1.8.0.255", lambda: energy("1.0.1.8.0.255"))
        for t in range(1, args.tariffs + 1):
            name = f"1.0.1.8.{t}.255"
            show(f"A+ T{t} {name}", lambda name=name: energy(name))

        if args.scan:
            print("\nпоиск версии коммуникационного ПО")
            names = [f"0.0.96.1.{k}.255" for k in range(4, 10)]
            names += ["1.0.0.2.0.255", "0.0.0.2.0.255", "1.0.0.2.8.255", "0.0.96.1.10.255"]
            for name in names:
                show(f"класс 1 {name}", lambda name=name: (
                    lambda v: text(v) if isinstance(v, bytes) else repr(v))(get(link, 1, name, 2)))
    except LinkError as e:
        sys.exit(f"ошибка канала: {e}")
    finally:
        if link:
            link.disc()
        ser.close()


if __name__ == "__main__":
    main()
