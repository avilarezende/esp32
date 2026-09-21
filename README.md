# esp32

Starter firmware for the [ESP32](https://www.espressif.com/en/products/socs/esp32)
built with [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32/index.html).

On boot the device connects to the Wi-Fi network whose credentials are stored
in NVS. When no credentials are stored (first boot) or the connection fails, it
starts a **SoftAP configuration portal** (`ESP32-Setup`) that serves a small web
page where you enter the network to join. The credentials are saved to NVS and
the device reboots to connect.

The firmware runs on real ESP32 hardware and, for the configuration portal, can
be exercised end to end under the ESP-IDF QEMU emulator.

## Requirements

- ESP-IDF **v5.3.2** and its `esp32` toolchain
- The `qemu-xtensa` tool (bundled QEMU build shipped by Espressif) for emulation

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

## Wi-Fi configuration flow

1. On boot the device reads the SSID/password from NVS.
2. If present, it joins that network as a station (retrying a few times).
3. If absent, or the join fails, it starts the `ESP32-Setup` SoftAP and serves a
   configuration page.
4. Connect a phone/laptop to `ESP32-Setup` (password `esp32setup`), open the
   device's IP in a browser, pick/enter your network, and submit.
5. The credentials are stored in NVS and the device reboots to connect.

The AP SSID/password are defined in [`main/wifi_manager.h`](main/wifi_manager.h).

## Build and flash (real hardware)

```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Run the configuration portal under QEMU

QEMU does **not** emulate the ESP32 Wi-Fi radio, so a dedicated build disables
the radio (`CONFIG_APP_ENABLE_WIFI_RADIO=n`, see
[`sdkconfig.qemu`](sdkconfig.qemu)) and serves the *same* configuration portal
over the emulated OpenCores Ethernet interface. This lets the web interface be
tested end to end without hardware.

Build and run the QEMU variant with host port `8080` forwarded to the device's
port `80`:

```bash
idf.py -B build_qemu -DSDKCONFIG=build_qemu/sdkconfig \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.qemu" build

qemu-system-xtensa -M esp32 -m 4M \
  -drive file=build_qemu/flash_image.bin,if=mtd,format=raw \
  -drive file=build_qemu/qemu_efuse.bin,if=none,format=raw,id=efuse \
  -global driver=nvram.esp32.efuse,property=drive,value=efuse \
  -global driver=timer.esp32.timg,property=wdt_disable,value=true \
  -nic user,model=open_eth,hostfwd=tcp::8080-:80 \
  -nographic -serial mon:stdio
```

(`build_qemu/flash_image.bin` and `build_qemu/qemu_efuse.bin` are generated the
first time you run `idf.py -B build_qemu qemu`.)

Then open <http://127.0.0.1:8080/> in a browser, or drive it from the shell:

```bash
curl http://127.0.0.1:8080/                       # fetch the config page
curl -X POST --data 'ssid=MyHomeWiFi&password=secret' \
     http://127.0.0.1:8080/connect                # submit credentials
```

The submitted credentials are persisted to NVS; power-cycle the emulator to see
the device report `found stored credentials for SSID '...'` on the next boot.

## Project layout

```
.
├── CMakeLists.txt          # top-level ESP-IDF project file
├── sdkconfig.defaults      # target (esp32), flash size, HTTP header limit, openeth
├── sdkconfig.qemu          # QEMU overlay: disables the Wi-Fi radio
├── main/
│   ├── CMakeLists.txt      # component registration + dependencies
│   ├── Kconfig.projbuild   # APP_ENABLE_WIFI_RADIO option
│   ├── main.c              # app_main: chip info + wifi_manager bring-up
│   ├── wifi_manager.[ch]   # STA-from-NVS, SoftAP provisioning, credential storage
│   ├── http_server.[ch]    # configuration web portal (GET / and POST /connect)
│   └── qemu_eth.[ch]       # emulated OpenCores Ethernet bring-up (QEMU only)
└── .cursor/
    ├── environment.json    # Cloud Agent environment definition
    └── install.sh          # idempotent toolchain + QEMU bootstrap
```
