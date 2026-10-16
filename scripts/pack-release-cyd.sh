#!/usr/bin/env bash
# Rebuild build_cyd and refresh release/cyd/ for cable flash and HTTP OTA.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [ -z "${IDF_PATH:-}" ]; then
  if [ -f "$HOME/esp/esp-idf/export.sh" ]; then
    # shellcheck disable=SC1091
    . "$HOME/esp/esp-idf/export.sh" >/dev/null
  else
    echo "ESP-IDF not found. Source export.sh first." >&2
    exit 1
  fi
fi

idf.py -B build_cyd -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.cyd" build

mkdir -p release/cyd
cp -f build_cyd/bootloader/bootloader.bin release/cyd/bootloader.bin
cp -f build_cyd/partition_table/partition-table.bin release/cyd/partition-table.bin
cp -f build_cyd/esp32.bin release/cyd/esp32.bin
# Same application image, named for POST /ota (do not flash this at 0x0).
cp -f build_cyd/esp32.bin release/cyd/esp32-ota.bin

# Blank otadata selects ota_0. Writing it does not touch NVS at 0x9000.
python - <<'PY'
from pathlib import Path
Path("release/cyd/otadata.bin").write_bytes(b"\xff" * 0x2000)
PY

# Single image at 0x0 for web flashers. Gaps are 0xFF, so this image also
# covers the NVS region: a full-chip write wipes saved Wi-Fi and the hub.
# To keep NVS, use flash.sh / flash.bat (separate offsets) instead.
python -m esptool --chip esp32 merge_bin -o release/cyd/firmware-cyd.bin \
  --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x1000 release/cyd/bootloader.bin \
  0x8000 release/cyd/partition-table.bin \
  0xf000 release/cyd/otadata.bin \
  0x20000 release/cyd/esp32.bin

(
  cd release/cyd && sha256sum \
    bootloader.bin partition-table.bin otadata.bin \
    esp32.bin esp32-ota.bin firmware-cyd.bin > SHA256SUMS
)
chmod +x release/cyd/flash.sh scripts/pack-release-cyd.sh

echo "Updated release/cyd:"
ls -la release/cyd/
cat release/cyd/SHA256SUMS
