# 4. Прошивка

Пины: **GPIO18 = TX**, **GPIO19 = RX** (почему — в [03-esp32-c6-wiring.md](03-esp32-c6-wiring.md)).
Формат: 300 бод, 7E1 — старт режима C IEC 62056-21. Параметры конкретного
счётчика уточнить по его документации.

## ESPHome

```yaml
uart:
  id: opto
  tx_pin: GPIO18
  rx_pin: GPIO19
  baud_rate: 300
  data_bits: 7
  parity: EVEN
  stop_bits: 1
```

`data_bits` 5–8 и `parity: EVEN` поддерживаются; для инверсии уровня —
полная схема пина с `inverted: true` **[проверено: ESPHome UART]**.

Готовый компонент для IEC 62056-21 в ESPHome — у aquaticus (см. [sources.md](sources.md)).

## Arduino (arduino-esp32)

В меню платы включить **USB CDC On Boot → Enabled** (для ESP32-C6 по
умолчанию Disabled), чтобы `Serial` шёл по USB **[проверено: boards.txt]**.

```cpp
HardwareSerial Opto(1);                  // UART1

void setup() {
  Serial.begin(115200);                  // лог по USB
  Opto.begin(300, SERIAL_7E1, 19, 18);   // RX=GPIO19, TX=GPIO18
  Opto.print("/?!\r\n");                 // запрос идентификации IEC 62056-21
}

void loop() {
  while (Opto.available()) Serial.write(Opto.read());
}
```

`SERIAL_7E1` и `HardwareSerial::setRxInvert(bool)` есть в
`cores/esp32/HardwareSerial.h` **[проверено: arduino-esp32, master]**.

## Если счётчик молчит или отвечает мусором

1. Проверить TX: камера телефона видит мигание ИК-диода при `Opto.print`.
2. Проверить положение головки: окно оптопорта, магнит, зазор.
3. Инверсия: по разбору схем она не нужна (см. [01-optical-probe-circuit.md](01-optical-probe-circuit.md#полярность-инверсия-не-нужна)),
   а при подключении к линиям CP2102N полярность и так уартовая. Если всё же
   мусор — попробовать `Opto.setRxInvert(true)` / `inverted: true` на RX.
4. Скорость и формат — по документации счётчика.
