#!/usr/bin/env bash
#
# Build and run host unit tests natively with gcc (wifi_form and the
# screenshot BMP). Reuses the Unity framework bundled with ESP-IDF.
# No hardware or emulator required.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IDF="${IDF_PATH:-$HOME/esp/esp-idf}"
UNITY="$IDF/components/unity/unity/src"

if [ ! -f "$UNITY/unity.c" ]; then
    echo "error: Unity not found at $UNITY (set IDF_PATH)" >&2
    exit 1
fi

mkdir -p "$HERE/build"
gcc -std=c11 -Wall -Wextra \
    -I "$UNITY" \
    -I "$HERE/../components/wifi_form/include" \
    "$HERE/test_wifi_form.c" \
    "$HERE/../components/wifi_form/wifi_form.c" \
    "$UNITY/unity.c" \
    -o "$HERE/build/wifi_form_test"

"$HERE/build/wifi_form_test"

gcc -std=c11 -Wall -Wextra \
    -I "$UNITY" \
    -I "$HERE/../main" \
    "$HERE/test_cyd_bmp.c" \
    "$HERE/../main/cyd_bmp.c" \
    "$UNITY/unity.c" \
    -o "$HERE/build/cyd_bmp_test"

"$HERE/build/cyd_bmp_test"
