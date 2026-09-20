# 3. Подключение к ESP32-C6 Super Mini

## SoftwareSerial не нужен

- У ESP32-C6 **два UART в основной системе и один LP UART**; сигналы UART
  выводятся на любые GPIO через GPIO Matrix **[проверено: даташит ESP32-C6]**.
- USB на Super Mini — встроенный USB-Serial/JTAG на GPIO12 (D−) / GPIO13 (D+),
  USB-UART моста на плате нет **[проверено: даташит, studiopieters]**.
  Консоль идёт по USB и аппаратный UART не занимает.
- В ESPHome программный UART есть только на ESP8266, на ESP32 его нет;
  на ESP32 используются аппаратные UART **[проверено: ESPHome UART]**.

## USB головки к C6 не подключить

USB у ESP32-C6 — фиксированное устройство Serial/JTAG, «cannot be
reconfigured to perform any function other than a serial port and JTAG»;
USB OTG (хоста) нет **[проверено: ESP-IDF, USB Serial/JTAG Console]**.
Поэтому подключиться к D+/D− головки и говорить с CP2102N по USB C6 не может.
Вариант с USB-хостом — ESP32-S3, см. [05-alternatives.md](05-alternatives.md).

## Выбор пинов

**[проверено: даташит ESP32-C6, espboards]**

| Пины | Что там | Использовать? |
|---|---|---|
| GPIO8, GPIO9, GPIO15 | strapping; на 8 — RGB-светодиод, на 9 — кнопка BOOT, на 15 — светодиод статуса | нет |
| GPIO12, GPIO13 | USB D−/D+ | нет |
| GPIO4–7 | JTAG (MTMS/MTDI и др.) | лучше нет |
| GPIO16 / GPIO17 | U0TXD / U0RXD (UART0 по умолчанию) | можно |
| GPIO0–3, 14, 18–23 | без системных функций | **да** |

Выбраны **GPIO18 = TX**, **GPIO19 = RX** на UART1 — чтобы не пересекаться с
UART0.

## Распайка

| Головка | ESP32-C6 Super Mini |
|---|---|
| 5 В (провод кабеля) | 5V |
| GND (провод кабеля) | GND |
| TX — **катод ИК-диода** | GPIO18 |
| **180 Ом → анод ИК-диода** | **3V3** |
| RX — одиночная ножка V2 | GPIO19 |

Передатчик собран мимо драйвера головки: ИК-диод висит прямо на GPIO, ток
задаёт тот же резистор 180 Ом, что стоял на плате головки, но питается он
теперь с 3V3 платы. Зачем так и что резать —
[02-rixutech-cp2102n-teardown.md](02-rixutech-cp2102n-teardown.md#передатчик-ик-диод-напрямую-на-gpio).

## Питание

- Головка питается от 5 В: CP2102N делает себе 3,3 В встроенным регулятором
  (вход REGIN) **[проверено: даташит CP2102N]**.
- На пине 5V Super Mini при питании от USB-C есть 5 В **[гипотеза: зависит
  от разводки платы, возможно через диод — измерить]**.
- GPIO ESP32 — только 3,3 В **[проверено: rwanrooy]**. Уровень на RX — см.
  проверки в [02-rixutech-cp2102n-teardown.md](02-rixutech-cp2102n-teardown.md).
