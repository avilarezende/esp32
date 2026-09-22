#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SSID advertised by the device when it starts its configuration portal. */
#define WIFI_MANAGER_AP_SSID "ESP32-Setup"
/* Password for the configuration AP. Must be >= 8 chars, or "" for an open AP. */
#define WIFI_MANAGER_AP_PASS "esp32setup"

/* Maximum credential lengths (including the terminating NUL). */
#define WIFI_MANAGER_SSID_MAXLEN 33
#define WIFI_MANAGER_PASS_MAXLEN 65

typedef enum {
    WIFI_MANAGER_STATE_PROVISIONING = 0, /* portal is up, waiting for setup      */
    WIFI_MANAGER_STATE_CONNECTING,       /* trying to join the stored network    */
    WIFI_MANAGER_STATE_CONNECTED,        /* station has an IP                    */
} wifi_manager_state_t;

/*
 * Bring up Wi-Fi.
 *
 * If credentials are stored in NVS, the device tries to join that network as a
 * station and keeps it connected (auto-reconnecting on drops). When no
 * credentials are stored (or the initial join fails), it starts a SoftAP
 * configuration portal. Returns after the initial decision; background work
 * (portal, reconnection) continues afterwards.
 */
esp_err_t wifi_manager_start(void);

wifi_manager_state_t wifi_manager_get_state(void);
bool wifi_manager_is_connected(void);
bool wifi_manager_is_provisioning(void);

/* Current/last network name (empty string if none). */
const char *wifi_manager_get_ssid(void);
/* Most recent IP obtained on the active interface (empty string if none). */
const char *wifi_manager_get_ip(void);
/* Station signal strength in dBm, or 0 when unavailable (e.g. no radio). */
int wifi_manager_get_rssi(void);
/* Human-readable reason for the last station disconnect (empty if none). */
const char *wifi_manager_last_disconnect_reason(void);

/* Persist Wi-Fi credentials to NVS. Called by the HTTP configuration handler. */
esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password);
/* Load Wi-Fi credentials from NVS. Returns ESP_ERR_NVS_NOT_FOUND when unset. */
esp_err_t wifi_manager_load_credentials(char *ssid, size_t ssid_len,
                                        char *password, size_t password_len);
/* Erase stored credentials from NVS (used by the "forget network" action). */
esp_err_t wifi_manager_forget(void);

/*
 * Scan for nearby networks and write a JSON array to `buf`:
 *   [{"ssid":"Home","rssi":-52,"auth":3}, ...]
 * With the radio disabled (QEMU build) this returns a small set of clearly
 * labeled placeholder entries so the dynamic UI can be exercised without a
 * radio. Always writes a valid JSON array (at least "[]").
 */
esp_err_t wifi_manager_scan_json(char *buf, size_t buf_len);

/* State transitions used by the portal after the user acts. On real hardware
 * the device reboots to apply changes; under QEMU (no radio) these update the
 * in-memory state so the portal reflects the change immediately. */
void wifi_manager_mark_connected(const char *ssid);
void wifi_manager_mark_provisioning(void);

#ifdef __cplusplus
}
#endif
