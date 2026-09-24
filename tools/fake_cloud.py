#!/usr/bin/env python3
"""Заглушка облака Waterius — проверить отправку и OTA без бэкенда.

    python3 tools/fake_cloud.py [--port 8080] [--ota firmware.bin] [--ota-fs littlefs.bin]

На странице /settings в поле «Сервер» указать http://<IP компьютера>:8080.

POST в корень печатает тело запроса (путь принимается любой). С --ota / --ota-fs первый ответ
содержит блок "ota" (формат сервера Waterius), файлы раздаются по
GET /firmware/<имя>. Бинарники после сборки лежат в
.pio/build/<env>/firmware.bin и .pio/build/<env>/littlefs.bin.
"""
import argparse
import hashlib
import http.server
import json
import os


def md5(path):
    with open(path, 'rb') as f:
        return hashlib.md5(f.read()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=8080)
    parser.add_argument('--ota', help='firmware.bin для блока ota.firmware')
    parser.add_argument('--ota-fs', help='littlefs.bin для блока ota.filesystem')
    args = parser.parse_args()

    files = {os.path.basename(p): p for p in (args.ota, args.ota_fs) if p}
    state = {'ota_sent': False}

    def image(host, path):
        return {'url': f'http://{host}/firmware/{os.path.basename(path)}',
                'md5': md5(path), 'size': os.path.getsize(path)}

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_POST(self):
            body = self.rfile.read(int(self.headers.get('Content-Length', 0)))
            print(self.path, 'Waterius-Token:', self.headers.get('Waterius-Token'))
            try:
                print(json.dumps(json.loads(body), ensure_ascii=False, indent=2))
            except ValueError:
                print(body)

            reply = b''
            if files and not state['ota_sent']:
                host = self.headers.get('Host')
                ota = {}
                if args.ota:
                    ota['firmware'] = image(host, args.ota)
                if args.ota_fs:
                    ota['filesystem'] = image(host, args.ota_fs)
                reply = json.dumps({'ota': ota}).encode()
                state['ota_sent'] = True
                print('-> отправлен блок ota')

            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(reply)))
            self.end_headers()
            self.wfile.write(reply)

        def do_GET(self):
            name = self.path.rsplit('/', 1)[-1]
            if not self.path.startswith('/firmware/') or name not in files:
                self.send_error(404)
                return
            with open(files[name], 'rb') as f:
                data = f.read()
            print('GET', self.path, len(data), 'байт')
            self.send_response(200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)

    print(f'Заглушка облака на порту {args.port}')
    http.server.ThreadingHTTPServer(('0.0.0.0', args.port), Handler).serve_forever()


if __name__ == '__main__':
    main()
