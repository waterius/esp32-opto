#!/bin/sh
# Собирает по одному файлу на чип: bootloader + таблица разделов + otadata +
# приложение + образ LittleFS склеены в общий образ, который льётся с адреса 0.
# Такой файл понимают онлайн-заливщики (ESP Web Tools, esptool-js) — человеку
# не надо знать смещения.
#
#   sh tools/build_release.sh      # → dist/*.bin
#
# Смещения берутся из таблицы разделов окружения. Обе платы — 4 МБ и
# min_spiffs.csv: у S3 Super Mini на борту 4 МБ, и образ под 8 в неё не
# влезает (см. platformio.ini).
set -eu

PIO="$HOME/.platformio/penv/bin/pio"
PY="$HOME/.platformio/penv/bin/python"
ESPTOOL="$HOME/.platformio/packages/tool-esptoolpy/esptool.py"
BOOT_APP0="$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"

cd "$(dirname "$0")/.."
VERSION=$(sed -n 's/.*version = "\\"\(.*\)\\""/\1/p' platformio.ini)
mkdir -p dist

"$PIO" run -e esp32-s3 -e esp32-c3
"$PIO" run -e esp32-s3 -e esp32-c3 -t buildfs

# env | чип | размер флеша | смещение раздела с LittleFS
for row in "esp32-s3 esp32s3 4MB 0x3D0000" "esp32-c3 esp32c3 4MB 0x3D0000"; do
    set -- $row
    env=$1 chip=$2 flash=$3 fs_off=$4
    b=".pio/build/$env"
    out="dist/electrius-opto-$VERSION-$env.bin"
    "$PY" "$ESPTOOL" --chip "$chip" merge_bin -o "$out" \
        --flash_mode dio --flash_freq 80m --flash_size "$flash" \
        0x0      "$b/bootloader.bin" \
        0x8000   "$b/partitions.bin" \
        0xe000   "$BOOT_APP0" \
        0x10000  "$b/firmware.bin" \
        "$fs_off" "$b/littlefs.bin"
    echo "$out"
done
