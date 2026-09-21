#pragma once

#include <stdbool.h>
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

/*
 * Bring up Wi-Fi.
 *
 * If credentials are stored in NVS, the device tries to join that network as a
 * station. When no credentials are stored (or the join fails), it starts a
 * SoftAP configuration portal (see http_server.h) so the user can enter the
 * network to connect to. Returns once the initial decision has been made; the
 * portal, when started, keeps running in the background.
 */
esp_err_t wifi_manager_start(void);

/* True once the station interface has obtained an IP address. */
bool wifi_manager_is_connected(void);

/* True while the SoftAP configuration portal is active. */
bool wifi_manager_is_provisioning(void);

/* Persist Wi-Fi credentials to NVS. Called by the HTTP configuration handler. */
esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password);

/* Load Wi-Fi credentials from NVS. Returns ESP_ERR_NVS_NOT_FOUND when unset. */
esp_err_t wifi_manager_load_credentials(char *ssid, size_t ssid_len,
                                        char *password, size_t password_len);

#ifdef __cplusplus
}
#endif
