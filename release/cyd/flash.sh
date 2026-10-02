#!/usr/bin/env bash
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
PORT="${1:-/dev/ttyUSB0}"

echo "Gravando firmware CYD 2.8\" em ${PORT} ..."
python3 -m esptool --chip esp32 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m \
  0x1000 "${DIR}/bootloader.bin" \
  0x8000 "${DIR}/partition-table.bin" \
  0x10000 "${DIR}/esp32.bin"

echo
echo "Pronto. Monitor: idf.py -p ${PORT} monitor"
echo "Portal: Wi-Fi ESP32-Setup / esp32setup -> http://192.168.4.1"
