#pragma once

#include "esp_err.h"
#include <stddef.h>

/* Passive BLE scanner for the CYD. Wi-Fi + display stay primary; the radio
 * shares time with a 30/320 ms scan window, same duty cycle as the StickS3
 * ESPHome proxy. Tracks Spock's Find Hub tag and a short list of recent
 * advertisements for Home Assistant. */
esp_err_t cyd_ble_start(void);

/* 1 brings the controller and the passive scan up. 0 stops the scan and
 * deinits NimBLE and the controller so the heap comes back. Safe to call
 * from the HTTP task. A failed OTA calls this with 1; a successful OTA
 * reboots instead. */
esp_err_t cyd_ble_set_scan(int on);

/* 1 when the stack was torn down on purpose (manual pause or OTA). */
int cyd_ble_is_paused(void);

/* JSON for GET /ble. Safe to call before the scanner is up. */
void cyd_ble_json(char *buf, size_t n);
