# esp32-opto

Прошивка для чтения электросчётчика **НАРТИС-100** через оптопорт на
**ESP32-S3** и **ESP32-C3** с отправкой показаний в облако Waterius.

- чтение по СПОДЭС (DLMS/COSEM поверх HDLC) библиотекой GuruxDLMS.c;
- веб-морда: статус, настройки, Wi-Fi (портал перенесён из прошивки Waterius),
  лог в реальном времени;
- прозрачный serial по RFC 2217 в локальной сети;
- OTA: страница `/update`, ArduinoOTA, сервер Waterius.

Оптоголовка — доработанный кабель RIXUTECH на CP2102N, её разбор — в
[docs/](docs/README.md). Дизайн прошивки —
[docs/superpowers/specs/2026-09-17-esp32-opto-firmware-design.md](docs/superpowers/specs/2026-09-17-esp32-opto-firmware-design.md).

## Сборка и прошивка

```sh
~/.platformio/penv/bin/pio run -e esp32-s3 -t upload     # прошивка
~/.platformio/penv/bin/pio run -e esp32-s3 -t uploadfs   # веб-страницы
```

Для ESP32-C3 — то же с `-e esp32-c3`.

## Первое включение

1. Подключиться к сети Wi-Fi `esp32-opto-XXXX` — страница настройки откроется
   сама, иначе `http://192.168.4.1/wifi.html`.
2. Выбрать роутер и ввести пароль.
3. Адрес устройства в сети роутера — в списке клиентов роутера или в USB-логе.
   На `/settings` задать ключ и e-mail облака Waterius.

Сброс к заводским настройкам — удержать кнопку BOOT 5 секунд.

## Подключение головки

| Сигнал | ESP32-S3 | ESP32-C3 |
|---|---|---|
| RX (с головки) | GPIO17 | GPIO4 |
| TX (на головку) | GPIO18 | GPIO5 |

Питание головки и доработка платы — в [docs/02-rixutech-cp2102n-teardown.md](docs/02-rixutech-cp2102n-teardown.md).
