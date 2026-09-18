# Источники

## Схемы ИК-головок

- [twam/IEC-62056-21-Optical-Probe](https://github.com/twam/IEC-62056-21-Optical-Probe) — KiCad-проект, CP2102N + ИК-часть ([Schematic.pdf](https://github.com/twam/IEC-62056-21-Optical-Probe/blob/master/Schematic.pdf))
- [volkszaehler: IR-Schreib-Lesekopf, TTL-Ausgang](https://wiki.volkszaehler.org/hardware/controllers/ir-schreib-lesekopf-ttl-ausgang) — схема ИК-головки с TTL-выходом (CC BY-SA)
- [rwanrooy: IEC 62056-21 IR optical smart meter reader](https://rwanrooy.github.io/iec62056-21-IR-optical-smart-meter-reader/) — ИК-головка для ESP32, 3,3 В TTL, 7E1

## Даташиты и документация

- [CP2102N Data Sheet (Silicon Labs)](https://www.silabs.com/documents/public/data-sheets/cp2102n-datasheet.pdf) — выводы QFN28, поведение выводов при сбросе
- [CP2102/9 Data Sheet (Silicon Labs)](https://www.silabs.com/documents/public/data-sheets/CP2102-9.pdf)
- [ESP32-C6 Datasheet (Espressif)](https://documentation.espressif.com/esp32-c6_datasheet_en.html) — UART, GPIO Matrix, strapping, USB
- [ESP-IDF: USB Serial/JTAG Controller Console (ESP32-C6)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-guides/usb-serial-jtag-console.html) — нет USB OTG
- [ESP32-C6 Super Mini — espboards](https://www.espboards.dev/esp32/esp32-c6-super-mini/) — распиновка, безопасные пины
- [ESP32-C6 Super Mini — studiopieters](https://www.studiopieters.nl/the-ultimate-guide-to-the-esp32-c6-super-mini-pinout/) — встроенный USB CDC/JTAG
- [ESPHome UART](https://esphome.io/components/uart/)
- [ESPHome USB UART](https://esphome.io/components/usb_uart/)
- [ESPHome IEC 62056-21 component (aquaticus)](https://aquaticus.info/iec62056.html)
- [IEC 62056 — Wikipedia](https://en.wikipedia.org/wiki/IEC_62056)

## Товар

- [AliExpress: kWh Meter Infrared Reading Head IEC1107 Probe CP2102](https://www.aliexpress.com/item/1005004623593781.html)
- [Amazon: KWh Meter Infrared Reading Head IEC1107 CP2102](https://www.amazon.com/Infrared-Reading-Interface-ma-netic-IEC62056/dp/B0F63SB6TB)

## Счётчик НАРТИС

- [РЭ НАРТИС-100 НРДЛ.411152.003РЭ](https://cdn.tns-e.ru/iblock/75e/75e1acc8af3ec04732425482dc52bcdb/Rukovodstvo_po_ekspluatatsii_Nartis_100.pdf) — условное обозначение, оптопорт, СПОДЭС, заводские пароли (зеркало: на nartis.ru файл отдаёт 404)
- [СТО 34.01-5.1-006-2017 (СПОДЭС)](https://77cs.ru/f/cto_340151-006-2017.pdf) — HDLC, адреса клиента и сервера, режим E, OBIS
- [РЭ НАРТИС-И100 НРДЛ.411152.101РЭ](https://www.nartis.ru/upload/iblock/5f6/nmge5xsoiskcr0avk6yrwilnnctko9rp.pdf) — для сравнения с И-серией
- [яЭнергетик: сетевые адреса НАРТИС](https://yaenergetik.ru/blog/kak-konfigurirovat-schyotchiki-nartis-cherez-yaemost-nastrojka-setevyh-adresov-i-konfigurator/) — 16 у серии 100/300, 17 у И-серии
- [SAURES: подключение НАРТИС](https://www.saures.ru/kb/article-5991/)
- [latonita/esphome-dlms-cosem](https://github.com/latonita/esphome-dlms-cosem) — рабочий клиент СПОДЭС, разобран И100-W112
