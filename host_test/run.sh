#!/usr/bin/env bash
#
# Build and run host unit tests natively with gcc (wifi_form, the screenshot
# BMP and the CYD scene painter). Also renders page previews to
# host_test/build/preview. Reuses the Unity framework bundled with ESP-IDF.
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

gcc -std=c11 -Wall -Wextra \
    -I "$UNITY" \
    -I "$HERE/../main" \
    "$HERE/test_cyd_scene.c" \
    "$HERE/../main/cyd_scene.c" \
    "$UNITY/unity.c" \
    -o "$HERE/build/cyd_scene_test"

"$HERE/build/cyd_scene_test"

gcc -std=c11 -Wall -Wextra \
    -I "$UNITY" \
    -I "$HERE/../main" \
    "$HERE/test_cyd_net.c" \
    "$UNITY/unity.c" \
    -o "$HERE/build/cyd_net_test"

"$HERE/build/cyd_net_test"

gcc -std=c11 -Wall -Wextra \
    -I "$HERE/../main" \
    "$HERE/render_ui.c" \
    "$HERE/../main/cyd_scene.c" \
    "$HERE/../main/cyd_bmp.c" \
    -o "$HERE/build/render_ui"

mkdir -p "$HERE/build/preview"
"$HERE/build/render_ui" "$HERE/build/preview"
