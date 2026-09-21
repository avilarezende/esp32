# esp32

Starter firmware for the [ESP32](https://www.espressif.com/en/products/socs/esp32)
built with [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32/index.html).

The firmware prints the chip information on boot and then emits a heartbeat
message every second. It runs on real ESP32 hardware and under the ESP-IDF
QEMU emulator, so it can be built and exercised end to end without a physical
board.

## Requirements

- ESP-IDF **v5.3.2** and its `esp32` toolchain
- The `qemu-xtensa` tool (bundled QEMU build shipped by Espressif)

On a Cloud Agent these are installed automatically by
[`.cursor/install.sh`](.cursor/install.sh). To set them up manually, follow the
[ESP-IDF Get Started guide](https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32/get-started/index.html)
or run that script.

## Activate the toolchain

Every shell needs the ESP-IDF environment on its `PATH`:

```bash
. "$HOME/esp/esp-idf/export.sh"
```

The Cloud Agent bootstrap adds this line to `~/.bashrc`, so interactive shells
already have `idf.py` available.

## Build

```bash
idf.py build
```

## Run in the emulator

No hardware required — run the firmware under QEMU and watch the serial output:

```bash
idf.py qemu
```

Expected output (after the bootloader log):

```
Hello from the esp32 project!
Chip: esp32 with 2 CPU core(s), features: WiFi BT BLE, revision v3.0
Flash size: 4 MB
Free heap: 305072 bytes
heartbeat #0 - firmware alive
heartbeat #1 - firmware alive
...
```

Press `Ctrl-A X` to quit QEMU.

## Flash to real hardware

With a board connected over USB:

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

## Project layout

```
.
├── CMakeLists.txt          # top-level ESP-IDF project file
├── sdkconfig.defaults      # default target (esp32) and flash size
├── main/
│   ├── CMakeLists.txt      # component registration
│   └── hello_world_main.c  # application entry point (app_main)
└── .cursor/
    ├── environment.json    # Cloud Agent environment definition
    └── install.sh          # idempotent toolchain + QEMU bootstrap
```
