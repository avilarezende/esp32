#!/usr/bin/env bash
# Rebuild build_cyd and refresh release/cyd/ for offline flashing.
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
(cd release/cyd && sha256sum bootloader.bin partition-table.bin esp32.bin > SHA256SUMS)
chmod +x release/cyd/flash.sh scripts/pack-release-cyd.sh

echo "Updated release/cyd:"
ls -la release/cyd/
cat release/cyd/SHA256SUMS
