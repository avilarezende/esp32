#pragma once

#include "esp_err.h"
#include <stddef.h>

/* Passive BLE scanner for the CYD. Wi-Fi + display stay primary; the radio
 * shares time with a 30/320 ms scan window, same duty cycle as the StickS3
 * ESPHome proxy. Tracks Spock's Find Hub tag and a short list of recent
 * advertisements for Home Assistant. */
esp_err_t cyd_ble_start(void);

/* JSON for GET /ble. Safe to call before the scanner is up. */
void cyd_ble_json(char *buf, size_t n);
