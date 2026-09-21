#!/usr/bin/env bash
#
# Idempotent Cloud Agent bootstrap for this ESP32 (ESP-IDF) project.
#
# Installs system prerequisites, the ESP-IDF toolchain, and the QEMU emulator,
# wires up `idf.py` in interactive shells, and builds the firmware so the
# workspace is immediately ready to build and emulate.
set -euo pipefail

IDF_TARGET="esp32"
IDF_BRANCH="v5.3.2"
ESP_DIR="$HOME/esp"
IDF_PATH="$ESP_DIR/esp-idf"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

echo "==> Installing system prerequisites"
export DEBIAN_FRONTEND=noninteractive
sudo apt-get update -y
# ESP-IDF build deps plus the shared libraries the bundled QEMU links against
# (libslirp / glib / pixman).
sudo apt-get install -y --no-install-recommends \
  git wget flex bison gperf \
  python3 python3-pip python3-venv \
  cmake ninja-build ccache \
  libffi-dev libssl-dev dfu-util libusb-1.0-0 ca-certificates \
  libslirp0 libglib2.0-0 libpixman-1-0

echo "==> Fetching ESP-IDF ${IDF_BRANCH}"
mkdir -p "$ESP_DIR"
if [ ! -d "$IDF_PATH/.git" ]; then
  git clone -b "$IDF_BRANCH" --depth 1 --shallow-submodules --recursive \
    https://github.com/espressif/esp-idf.git "$IDF_PATH"
else
  echo "ESP-IDF already present at $IDF_PATH, skipping clone"
fi

echo "==> Installing ESP-IDF toolchain for ${IDF_TARGET} + QEMU"
"$IDF_PATH/install.sh" "$IDF_TARGET"
# QEMU is not part of install.sh's default target tools, so request it explicitly.
python3 "$IDF_PATH/tools/idf_tools.py" install qemu-xtensa

echo "==> Enabling idf.py in interactive shells"
BASHRC="$HOME/.bashrc"
MARKER="# >>> esp-idf export >>>"
if ! grep -qF "$MARKER" "$BASHRC" 2>/dev/null; then
  {
    echo ""
    echo "$MARKER"
    echo 'export IDF_PATH="$HOME/esp/esp-idf"'
    echo '[ -f "$IDF_PATH/export.sh" ] && . "$IDF_PATH/export.sh" >/dev/null 2>&1 || true'
    echo "# <<< esp-idf export <<<"
  } >> "$BASHRC"
fi

echo "==> Building firmware"
# shellcheck disable=SC1091
. "$IDF_PATH/export.sh" >/dev/null 2>&1
cd "$REPO_ROOT"
idf.py build

echo "==> Bootstrap complete. Run 'idf.py qemu' to emulate the firmware."
