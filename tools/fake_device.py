#!/usr/bin/env python3
"""Симулятор устройства: веб-страницы из data/ и API прошивки без платы.

    python3 tools/fake_device.py [--port 8000] [--data data]

Открыть http://127.0.0.1:8000/ — те же четыре страницы, что на плате:
статус, настройки, Wi-Fi и лог. Значения счётчика задаются на странице
http://127.0.0.1:8000/sim.

Протокол счётчика не эмулируется: «чтение» выжидает заданное время, пишет
в лог правдоподобные кадры оптопорта и подставляет значения из симулятора.
Отправка в облако настоящая — по адресу из настроек, так что пара с
tools/fake_cloud.py работает целиком.
"""
import argparse
import json
import os
import random
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

FIRMWARE_VERSION = 'sim'
BAUDS = [300, 600, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200]
START = time.monotonic()


def uptime_ms():
    return int((time.monotonic() - START) * 1000)


# --- кольцевой буфер лога: тот же формат, что у src/port/log.cpp ---

class Log:
    SIZE = 16384

    def __init__(self):
        self.lock = threading.Lock()
        self.buf = bytearray()
        self.base = 0          # позиция первого байта буфера в общем потоке
        self.line_start = True
        self.boot = random.getrandbits(32)

    def println(self, text=''):
        self.write(text + '\n')

    def write(self, text):
        data = text.encode()
        with self.lock:
            for part in data.splitlines(keepends=True):
                if self.line_start:
                    ms = uptime_ms()
                    self.buf += ('[%d.%03d] ' % (ms // 1000, ms % 1000)).encode()
                self.buf += part
                self.line_start = part.endswith(b'\n')
            extra = len(self.buf) - self.SIZE
            if extra > 0:
                del self.buf[:extra]
                self.base += extra
        print(text, end='' if text.endswith('\n') else '\n')

    def read(self, frm):
        with self.lock:
            total = self.base + len(self.buf)
            if frm > total:
                frm = 0
            skipped = False
            if frm < self.base:
                skipped = frm > 0
                nl = self.buf.find(b'\n')         # обрезанную строку не отдаём
                frm = self.base + (nl + 1 if nl >= 0 else len(self.buf))
            text = bytes(self.buf[frm - self.base:]).decode(errors='replace')
            return {'boot': self.boot, 'next': total, 'skipped': skipped, 'text': text}


log = Log()

# --- состояние ---

SETTINGS = {
    'baud': 9600, 'bits': 8, 'parity': 'N', 'stop': 1,
    'meter_enabled': True, 'meter_addr': 0, 'meter_pwd': '111',
    'period_min': 60, 'host': 'http://127.0.0.1:8080', 'key': 'sim-key', 'email': '',
    'rfc_enabled': True, 'rfc_port': 2217,
    'reboot_min': 60, 'ip': '', 'gateway': '', 'mask': '', 'dns': '',
}

# Что «увидит» прошивка при чтении. Меняется со страницы /sim.
SIM = {
    'serial': '52207839', 'model': 'НАРТИС-100.121RL', 'meter_fw': '255.06',
    'meter_time': '',                  # пусто — время компьютера
    'total': 1661.974, 'tariffs': [1150.614, 511.360, 0.0, 0.0], 'tariff_count': 4,
    'step': 0.05,                      # прибавка к сумме и T1 за каждое чтение
    'read_ms': 2000,
    'read_result': 'ok',               # ok | failed | auth | aborted
    'read_error': 'нет ответа счётчика',
    'transparent': False,              # порт занят прозрачной сессией
    'safe_mode': False,                # усечённый режим после серии неудачных загрузок
    'wifi_drops': 0,                   # разрывов Wi-Fi с момента загрузки
    'rssi': -62, 'ip': '192.168.1.55', 'ssid': 'HomeNet',
    'wifi_status': 'connected', 'wifi_error': '',  # failed + текст — как при отказе роутера
}

DEV = {
    'reading': False, 'meter_error': '',
    'has_reading': False, 'read_at': 0,
    'last': {'serial': '', 'model': '', 'meter_fw': '', 'meter_time': '',
             'total': 0.0, 'tariffs': []},
    'cloud_at': 0, 'cloud_code': 0, 'cloud_error': '',
    'read_now': False, 'send_now': False,
    'period_start': time.monotonic() - 3540,   # первый цикл через 60 с, как FIRST_CYCLE_MS
}

lock = threading.Lock()

# Кадры из docs/06-nartis-100-exchange.md — в лог идут байт в байт, как у OptoBus.
FRAMES = [
    ('TX', '7e a0 08 02 21 41 93 50 b4 7e'),
    ('RX', '7e a0 21 41 02 21 73 db d8 81 80 14 05 02 01 00 06 02 01 00 07 04 00 00 00 01 08 04 00 00 00 01 69 6d 7e'),
    ('TX', '7e a0 36 02 21 41 10 4a 3b e6 e6 00 60 31 a1 09 06 07 60 85 74 05 08 01 01 8a 02 07 80'),
    ('RX', '7e a0 3a 41 02 21 30 8c 2b e6 e7 00 61 29 a1 09 06 07 60 85 74 05 08 01 01 a2 03 02 01 00'),
    ('TX', '7e a0 19 02 21 41 32 f7 0d e6 e6 00 c0 01 c1 00 03 01 00 01 08 00 ff 02 00 6a 1c 7e'),
    ('RX', '7e a0 1c 41 02 21 52 12 6f e6 e7 00 c4 01 c1 00 06 00 19 5c 96 4c 8b 7e'),
]


def fmt_meter_time():
    return SIM['meter_time'] or time.strftime('%Y-%m-%d %H:%M:%S')


def do_read():
    """Чтение счётчика: выдержка, кадры в лог, результат из симулятора."""
    with lock:
        if not SETTINGS['meter_enabled']:
            return
        if SIM['transparent']:
            DEV['meter_error'] = 'чтение прервано прозрачной сессией'
            log.println('Счётчик: чтение прервано прозрачной сессией')
            return
        DEV['reading'] = True
        delay = max(SIM['read_ms'], 0) / 1000.0
        result = SIM['read_result']

    log.println('Оптопорт: %d %d%s%d' % (SETTINGS['baud'], SETTINGS['bits'],
                                         SETTINGS['parity'], SETTINGS['stop']))
    pause = delay / (len(FRAMES) + 1)
    for direction, hexbytes in FRAMES:
        time.sleep(pause)
        data = hexbytes.split()
        for off in range(0, len(data), 20):     # строка не длиннее 20 байт, как в OptoBus
            log.println(direction + ' ' + ' '.join(data[off:off + 20]))
    time.sleep(pause)

    with lock:
        DEV['reading'] = False
        if result == 'ok':
            SIM['total'] += SIM['step']
            if SIM['tariffs']:
                SIM['tariffs'][0] += SIM['step']
            n = max(0, min(SIM['tariff_count'], len(SIM['tariffs'])))
            DEV['last'] = {
                'serial': SIM['serial'], 'model': SIM['model'], 'meter_fw': SIM['meter_fw'],
                'meter_time': fmt_meter_time(), 'total': round(SIM['total'], 3),
                'tariffs': [round(v, 3) for v in SIM['tariffs'][:n]],
            }
            DEV['read_at'] = int(time.time())
            DEV['has_reading'] = True
            DEV['meter_error'] = ''
            log.println('Счётчик: всего %.3f кВт·ч, тарифов %d, sn %s'
                        % (DEV['last']['total'], len(DEV['last']['tariffs']), SIM['serial']))
        elif result == 'auth':
            DEV['meter_error'] = 'счётчик отверг пароль'
            SETTINGS['meter_enabled'] = False
            log.println('Счётчик: счётчик отверг пароль, опрос выключен')
        elif result == 'aborted':
            DEV['meter_error'] = 'чтение прервано прозрачной сессией'
            log.println('Счётчик: чтение прервано прозрачной сессией')
        else:
            DEV['meter_error'] = SIM['read_error']
            log.println('Счётчик: %s, повтор через 5 минут' % SIM['read_error'])


def cloud_payload():
    last = DEV['last']
    body = {
        'key': SETTINGS['key'], 'email': SETTINGS['email'], 'sn': last['serial'],
        'total': last['total'], 'data_type': 2,
    }
    for i, v in enumerate(last['tariffs'][:4]):
        body['total%d' % (i + 1)] = v
        body['data_type%d' % (i + 1)] = [5, 6, 7, 8][i]
    read_at = DEV['read_at']
    body.update({
        'fw': FIRMWARE_VERSION, 'model': last['model'], 'meter_fw': last['meter_fw'],
        'meter_time': last['meter_time'],
        'meter_read_at': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime(read_at)) if read_at else '',
        'ota_error': 0, 'chip_id': 0x515A0000, 'ip': SIM['ip'], 'rssi': SIM['rssi'],
    })
    return body


def do_send():
    with lock:
        if not DEV['has_reading']:
            DEV['cloud_error'] = 'нет данных счётчика'
            return
        if not SETTINGS['key']:
            DEV['cloud_error'] = 'ключ облака не задан'
            return
        url = SETTINGS['host'].rstrip('/') + '/api/source/iz/'
        body = json.dumps(cloud_payload(), ensure_ascii=False).encode()
        headers = {'Content-Type': 'application/json',
                   'Waterius-Token': SETTINGS['key'], 'Waterius-Email': SETTINGS['email']}

    req = urllib.request.Request(url, data=body, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=12) as res:
            code, text = res.status, res.read().decode(errors='replace')
    except urllib.error.HTTPError as e:
        code, text = e.code, e.read().decode(errors='replace')
    except Exception as e:                       # адрес недоступен — как code < 0 в прошивке
        code, text = -1, str(e)

    with lock:
        DEV['cloud_code'] = code
        log.println('Облако: HTTP %d %s' % (code, text.strip()))
        if code != 200:
            DEV['cloud_error'] = 'нет соединения' if code < 0 else 'сервер ответил ошибкой'
            return
        DEV['cloud_at'] = int(time.time())
        DEV['cloud_error'] = ''
        if '"ota"' in text:
            log.println('OTA: в симуляторе обновление не выполняется')


def worker():
    """Автомат опроса: тот же порядок, что в src/poller.cpp."""
    while True:
        time.sleep(0.1)
        with lock:
            read_now, DEV['read_now'] = DEV['read_now'], False
            transparent = SIM['transparent']
        if read_now:
            do_read()
            continue
        # Пока идёт прозрачная сессия, цикл откладывается целиком: запрошенная
        # отправка и наступивший период не теряются, а сработают после её конца
        if transparent:
            continue
        with lock:
            send_now, DEV['send_now'] = DEV['send_now'], False
            due = time.monotonic() - DEV['period_start'] >= SETTINGS['period_min'] * 60
            if due:
                DEV['period_start'] = time.monotonic()
        if send_now or due:
            do_read()
            do_send()


def seconds_to_next_send():
    left = SETTINGS['period_min'] * 60 - (time.monotonic() - DEV['period_start'])
    return int(max(left, 0))


def status():
    with lock:
        last = DEV['last']
        return {
            'fw': FIRMWARE_VERSION, 'ip': SIM['ip'], 'rssi': SIM['rssi'],
            'uptime_s': uptime_ms() // 1000, 'heap': 180000 + random.randint(0, 4000),
            'wifi_mode': 'STA', 'safe_mode': SIM['safe_mode'],
            'wifi_drops': SIM['wifi_drops'], 'wifi_offline_s': 0, 'link_alive': True, 'link_armed': True,
            'boot_reason': 'подано питание',
            'meter_enabled': SETTINGS['meter_enabled'], 'meter_reading': DEV['reading'],
            'meter_error': DEV['meter_error'], 'transparent': SIM['transparent'],
            'transparent_idle_s': 0,
            'has_reading': DEV['has_reading'], 'read_at': DEV['read_at'],
            'serial': last['serial'], 'model': last['model'], 'meter_fw': last['meter_fw'],
            'meter_time': last['meter_time'], 'total': last['total'], 'tariffs': last['tariffs'],
            'cloud_at': DEV['cloud_at'], 'cloud_code': DEV['cloud_code'],
            'cloud_error': DEV['cloud_error'], 'cloud_next_s': seconds_to_next_send(),
        }


# --- разбор форм: те же проверки и тексты ошибок, что в src/port/web.cpp ---

def is_ipv4(value):
    parts = value.split('.')
    return len(parts) == 4 and all(p.isdigit() and 0 <= int(p) <= 255 for p in parts)


def post_settings(form):
    errors, s = {}, dict(SETTINGS)

    def num(name, lo, hi, message):
        try:
            v = int(form.get(name, [''])[0])
        except ValueError:
            errors[name] = message
            return None
        if not lo <= v <= hi:
            errors[name] = message
            return None
        return v

    baud = num('baud', 300, 115200, 'Выберите скорость из списка')
    if baud is not None:
        if baud in BAUDS:
            s['baud'] = baud
        else:
            errors['baud'] = 'Выберите скорость из списка'
    bits = num('bits', 5, 8, 'Биты данных: от 5 до 8')
    if bits is not None:
        s['bits'] = bits
    parity = form.get('parity', [''])[0]
    if parity in ('N', 'E', 'O'):
        s['parity'] = parity
    else:
        errors['parity'] = 'Выберите чётность'
    stop = num('stop', 1, 2, 'Стоп-биты: 1 или 2')
    if stop is not None:
        s['stop'] = stop

    s['meter_enabled'] = form.get('meter_enabled', ['0'])[0] == '1'
    addr = num('meter_addr', 0, 127, 'Адрес: от 0 до 127')
    if addr is not None:
        s['meter_addr'] = addr
    pwd = form.get('meter_pwd', [''])[0].strip()
    if len(pwd) > 16:
        errors['meter_pwd'] = 'Пароль — до 16 символов'
    else:
        s['meter_pwd'] = pwd

    period = num('period_min', 1, 1440, 'Период: от 1 до 1440 минут')
    if period is not None:
        s['period_min'] = period
    host = form.get('host', [''])[0].strip()
    if len(host) > 63:
        errors['host'] = 'Адрес сервера — до 63 символов'
    elif not host.startswith(('http://', 'https://')):
        errors['host'] = 'Адрес начинается с http:// или https://'
    else:
        s['host'] = host
    for name, cap, message in (('key', 40, 'Ключ — до 40 символов'),
                               ('email', 63, 'E-mail — до 63 символов')):
        v = form.get(name, [''])[0].strip()
        if len(v) > cap:
            errors[name] = message
        else:
            s[name] = v

    s['rfc_enabled'] = form.get('rfc_enabled', ['0'])[0] == '1'
    port = num('rfc_port', 1, 65535, 'Порт: от 1 до 65535')
    if port is not None:
        s['rfc_port'] = port

    reboot_min = num('reboot_min', 0, 1440, 'От 0 до 1440 минут; 0 — не перезагружаться')
    if reboot_min is not None:
        s['reboot_min'] = reboot_min

    # Те же проверки адресов, что в paramIp()/postSettings в src/port/web.cpp
    for name in ('ip', 'gateway', 'mask', 'dns'):
        value = form.get(name, [''])[0].strip()
        if value and not is_ipv4(value):
            errors[name] = 'Адрес вида 192.168.1.10 или пусто'
        else:
            s[name] = value
    if 'ip' not in errors and s['ip'] and not (s['gateway'] and s['mask']):
        errors['ip'] = 'Со статическим адресом нужны шлюз и маска'

    if errors:
        return {'errors': errors}
    with lock:
        # core::needsRestart в src/core/settings.h
        reboot = any(s[k] != SETTINGS[k] for k in ('rfc_enabled', 'rfc_port', 'ip', 'gateway', 'mask', 'dns'))
        enabled_now = s['meter_enabled'] and not SETTINGS['meter_enabled']
        SETTINGS.update(s)
        if enabled_now:
            DEV['meter_error'] = ''
    log.println('Настройки сохранены')
    return {'ok': True, 'reboot': reboot}


def post_sim(form):
    def get(name, cast, default):
        try:
            return cast(form.get(name, [''])[0])
        except ValueError:
            return default

    with lock:
        SIM['serial'] = form.get('serial', [''])[0].strip()
        SIM['model'] = form.get('model', [''])[0].strip()
        SIM['meter_fw'] = form.get('meter_fw', [''])[0].strip()
        SIM['meter_time'] = form.get('meter_time', [''])[0].strip()
        SIM['total'] = get('total', float, SIM['total'])
        SIM['tariffs'] = [get('t%d' % (i + 1), float, SIM['tariffs'][i]) for i in range(4)]
        SIM['tariff_count'] = max(0, min(get('tariff_count', int, SIM['tariff_count']), 4))
        SIM['step'] = get('step', float, SIM['step'])
        SIM['read_ms'] = max(0, get('read_ms', int, SIM['read_ms']))
        SIM['read_result'] = form.get('read_result', ['ok'])[0]
        SIM['read_error'] = form.get('read_error', [''])[0].strip()
        SIM['rssi'] = get('rssi', int, SIM['rssi'])
        SIM['ip'] = form.get('ip', [''])[0].strip()
        was = SIM['transparent']
        SIM['transparent'] = form.get('transparent', ['0'])[0] == '1'
        if SIM['transparent'] != was:
            log.println('RFC 2217: ' + ('клиент подключился, порт занят' if SIM['transparent']
                                        else 'клиент отключился, порт свободен'))
    return {'ok': True}


SIM_PAGE = """<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>esp32-opto — симулятор</title>
<link rel="stylesheet" href="/style.css">
<script src="/app.js"></script>
</head>
<body onload="simPage()">
<div class="wrap">
    <nav class="nav">
        <a href="/">Статус</a>
        <a href="/settings.html">Настройки</a>
        <a href="/wifi.html">Wi-Fi</a>
        <a href="/log.html">Лог</a>
        <a class="on" href="/sim">Симулятор</a>
    </nav>
    <h2>Счётчик</h2>
    <p class="text">Значения, которые «прочитает» прошивка. Страницы устройства их не показывают,
    пока не прошло чтение — нажмите «Прочитать сейчас» на статусе.</p>
    <form id="sim" onsubmit="saveSim(event, this)">
        <label for="serial">Серийный номер</label>
        <input id="serial" name="serial">
        <label for="model">Модель</label>
        <input id="model" name="model">
        <label for="meter_fw">Версия ПО счётчика</label>
        <input id="meter_fw" name="meter_fw">
        <label for="meter_time">Время счётчика (пусто — время компьютера)</label>
        <input id="meter_time" name="meter_time" placeholder="2026-09-18 12:00:00">
        <label for="total">Всего, кВт·ч</label>
        <input id="total" name="total" type="number" step="0.001">
        <label for="t1">T1, кВт·ч</label>
        <input id="t1" name="t1" type="number" step="0.001">
        <label for="t2">T2, кВт·ч</label>
        <input id="t2" name="t2" type="number" step="0.001">
        <label for="t3">T3, кВт·ч</label>
        <input id="t3" name="t3" type="number" step="0.001">
        <label for="t4">T4, кВт·ч</label>
        <input id="t4" name="t4" type="number" step="0.001">
        <label for="tariff_count">Сколько тарифов отдавать</label>
        <input id="tariff_count" name="tariff_count" type="number" min="0" max="4">
        <label for="step">Прибавка к сумме и T1 за каждое чтение, кВт·ч</label>
        <input id="step" name="step" type="number" step="0.001">

        <h2>Поведение</h2>
        <label for="read_ms">Длительность чтения, мс</label>
        <input id="read_ms" name="read_ms" type="number" min="0">
        <label for="read_result">Результат чтения</label>
        <select id="read_result" name="read_result">
            <option value="ok">успех</option>
            <option value="failed">ошибка связи</option>
            <option value="auth">счётчик отверг пароль</option>
            <option value="aborted">прервано прозрачной сессией</option>
        </select>
        <label for="read_error">Текст ошибки связи</label>
        <input id="read_error" name="read_error">
        <div class="chk">
            <input type="checkbox" id="transparent" name="transparent">
            <label for="transparent">Порт занят прозрачной сессией RFC 2217</label>
        </div>
        <p class="text">Пока порт занят, опрос и отправка откладываются — как в прошивке.</p>

        <h2>Устройство</h2>
        <label for="ip">IP</label>
        <input id="ip" name="ip">
        <label for="rssi">Сигнал Wi-Fi, дБм</label>
        <input id="rssi" name="rssi" type="number">

        <button class="btn" type="submit">Применить</button>
        <p id="sim-saved" class="ok hd"></p>
    </form>
</div>
<script>
function simPage() {
    ajax('/api/sim', {}, s => {
        const form = $('sim');
        for (const k in s) {
            const inp = form.elements[k];
            if (!inp) continue;
            if (inp.type == 'checkbox') inp.checked = !!s[k];
            else inp.value = s[k];
        }
    });
}
function saveSim(event, form) {
    $('sim-saved').classList.add('hd');
    formSubmit(event, form, '/api/sim', () => showOk('sim-saved', 'Применено'));
}
</script>
</body>
</html>
"""

UPDATE_PAGE = ('<!DOCTYPE html><html lang="ru"><head><meta charset="utf-8">'
               '<link rel="stylesheet" href="/style.css"></head><body><div class="wrap">'
               '<h2>Обновление</h2><p class="text">В симуляторе страницы ElegantOTA нет: '
               'прошивать нечего.</p><a href="/">К статусу</a></div></body></html>')

NETWORKS = [
    {'ssid': 'HomeNet', 'level': 4, 'wifi_channel': 6, 'bssid': 'aa:bb:cc:dd:ee:01', 'open': False},
    {'ssid': 'Guest', 'level': 2, 'wifi_channel': 11, 'bssid': 'aa:bb:cc:dd:ee:02', 'open': True},
    {'ssid': 'Neighbour 2G', 'level': 1, 'wifi_channel': 1, 'bssid': 'aa:bb:cc:dd:ee:03', 'open': False},
]


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass                                     # свой лог ведём через Log

    def reply(self, code, ctype, body):
        if isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-cache')
        self.end_headers()
        self.wfile.write(body)

    def json(self, obj):
        self.reply(200, 'application/json', json.dumps(obj, ensure_ascii=False))

    def do_GET(self):
        url = urlparse(self.path)
        path, query = url.path, parse_qs(url.query)

        if path == '/api/status':
            return self.json(status())
        if path == '/api/settings':
            with lock:
                return self.json(dict(SETTINGS))
        if path == '/api/log':
            try:
                frm = int(query.get('from', ['0'])[0])
            except ValueError:
                frm = 0
            return self.json(log.read(frm))
        if path == '/api/sim':
            with lock:
                s = dict(SIM)
            for i in range(4):
                s['t%d' % (i + 1)] = round(s['tariffs'][i], 3)
            s['total'] = round(s['total'], 3)
            del s['tariffs']
            return self.json(s)
        if path == '/api/wifi_status':
            with lock:
                return self.json({'status': SIM['wifi_status'], 'error': SIM['wifi_error'],
                                  'ssid': SIM['ssid'], 'ip': SIM['ip'],
                                  'rssi': SIM['rssi'], 'mode': 'STA'})
        if path == '/api/networks':
            return self.json(NETWORKS)
        if path == '/sim':
            return self.reply(200, 'text/html; charset=utf-8', SIM_PAGE)
        if path == '/update':
            return self.reply(200, 'text/html; charset=utf-8', UPDATE_PAGE)
        return self.static(path)

    def do_POST(self):
        length = int(self.headers.get('Content-Length', 0))
        form = parse_qs(self.rfile.read(length).decode(), keep_blank_values=True)
        path = urlparse(self.path).path

        if path == '/api/settings':
            return self.json(post_settings(form))
        if path == '/api/sim':
            return self.json(post_sim(form))
        if path == '/api/read':
            with lock:
                DEV['read_now'] = True
            log.println('Запрошено чтение с веб-страницы')
            return self.json({'ok': True})
        if path == '/api/send':
            with lock:
                DEV['send_now'] = True
            log.println('Запрошена отправка с веб-страницы')
            return self.json({'ok': True})
        if path == '/api/reboot':
            log.println('Перезагрузка по команде с веб-страницы')
            threading.Thread(target=reboot, daemon=True).start()
            return self.json({'ok': True})
        if path == '/api/wifi':
            ssid = form.get('ssid', [''])[0]
            if not ssid or len(ssid) > 32:
                return self.json({'errors': {'ssid': 'Введите название сети, до 32 символов'}})
            with lock:
                SIM['ssid'] = ssid
            log.println('Wi-Fi: подключаюсь к «%s»' % ssid)
            return self.json({'ok': True})
        self.send_error(404)

    def static(self, path):
        name = 'index.html' if path == '/' else path.lstrip('/')
        full = os.path.normpath(os.path.join(DATA_DIR, name))
        if not full.startswith(DATA_DIR) or not os.path.isfile(full):
            return self.send_error(404)
        types = {'.html': 'text/html; charset=utf-8', '.js': 'application/javascript',
                 '.css': 'text/css'}
        with open(full, 'rb') as f:
            body = f.read()
        if full.endswith('.html'):
            # Ссылка на страницу симулятора — только в выдаче, файлы в data/ не трогаем
            body = body.decode().replace('</nav>', '<a href="/sim">Симулятор</a></nav>').encode()
        self.reply(200, types.get(os.path.splitext(full)[1], 'application/octet-stream'), body)


def reboot():
    """Перезагрузка: новый boot id и обнулённое время работы, настройки живут в NVS."""
    global START
    time.sleep(1)
    with lock:
        START = time.monotonic()
        log.boot = random.getrandbits(32)
        DEV.update(reading=False, read_now=False, send_now=False, period_start=time.monotonic() - 3540)
    log.println('Симулятор перезапущен')


def main():
    global DATA_DIR
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=8000)
    parser.add_argument('--data', default=os.path.join(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))), 'data'), help='каталог веб-страниц')
    args = parser.parse_args()
    DATA_DIR = os.path.abspath(args.data)

    threading.Thread(target=worker, daemon=True).start()
    log.println('Симулятор устройства запущен')
    print('Страницы: http://127.0.0.1:%d/   симулятор счётчика: http://127.0.0.1:%d/sim'
          % (args.port, args.port))
    ThreadingHTTPServer(('127.0.0.1', args.port), Handler).serve_forever()


if __name__ == '__main__':
    main()
