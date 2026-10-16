#!/usr/bin/env bash
# Last cable flash after the OTA partition change. Writes bootloader, the new
# table, blank otadata and the app at ota_0 (0x20000). Does not erase NVS
# (0x9000), so Wi-Fi, the hub token and the CYD settings survive.
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
PORT="${1:-/dev/ttyUSB0}"

echo "Gravando firmware CYD 2.8\" em ${PORT} (ota_0 @ 0x20000) ..."
python3 -m esptool --chip esp32 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m \
  0x1000 "${DIR}/bootloader.bin" \
  0x8000 "${DIR}/partition-table.bin" \
  0xf000 "${DIR}/otadata.bin" \
  0x20000 "${DIR}/esp32.bin"

echo
echo "Pronto. A NVS foi preservada. Monitor: idf.py -p ${PORT} monitor"
echo "Portal: Wi-Fi ESP32-Setup / esp32setup -> http://192.168.4.1"
echo "Proximas atualizacoes: POST /ota com esp32-ota.bin (sem cabo)."
