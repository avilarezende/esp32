#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bring up the OpenCores Ethernet MAC emulated by QEMU (`-nic user,model=open_eth`).
 *
 * This has no counterpart on real ESP32 hardware; it exists so the HTTP
 * configuration portal is reachable from the host when the firmware runs under
 * the ESP-IDF QEMU emulator (which does not emulate the Wi-Fi radio). It is
 * compiled only when CONFIG_ETH_USE_OPENETH is enabled and fails gracefully if
 * no such controller is present.
 */
esp_err_t qemu_eth_start(void);

#ifdef __cplusplus
}
#endif
