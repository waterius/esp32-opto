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
- [ESP32-C3 Datasheet (Espressif)](https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf) — GPIO18/19 = USB D−/D+
- [ESP32-C3-MINI-1 Datasheet (Espressif)](https://documentation.espressif.com/esp32-c3-mini-1_datasheet_en.html) — площадки модуля: 26 = IO18, 27 = IO19
- [ESP-IDF: USB Serial/JTAG Controller Console (ESP32-C3)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-guides/usb-serial-jtag-console.html) — прошивка и консоль через родной USB
- [ESP-IDF: Establish Serial Connection with ESP32-C3](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/get-started/establish-serial-connection.html) — вход в режим загрузки кнопкой BOOT
- [ESP-FAQ: USB](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/usb.html) — занятые под GPIO USB-пины отключают загрузку через USB
- [ESP Hardware Design Guidelines: Schematic Checklist (ESP32-C3)](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32c3/schematic-checklist.html) — последовательные 22/33 Ом резервируются, но не ставятся
- [ESP32-C3-DevKitM-1 User Guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c3/esp32-c3-devkitm-1/user_guide.html) — GPIO18/19 на гребёнке J3
- [ESP32-S3-DevKitC-1 v1.1 User Guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html) — GPIO19/20 = USB_D−/D+ на гребёнке J3
- [ESP32-C3 Super Mini — studiopieters](https://www.studiopieters.nl/esp32-c3-super-mini-pinout/) — USB-пины на гребёнку не выведены
- [ESP32-S3 Super Mini — espboards](https://www.espboards.dev/esp32/esp32-s3-super-mini/) — чип ESP32-S3FH4R2, выведенные пины
- [ESP32-C3 Super Mini — lastminuteengineers](https://lastminuteengineers.com/esp32-c3-super-mini-pinout-reference/) — 16 пинов, предупреждение о разной шелкографии у клонов
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
