#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Start the Wi-Fi configuration web server.
 *
 * Serves a small HTML form on `GET /` where the user can pick/enter an SSID and
 * password, and handles `POST /connect` by persisting the credentials (via
 * wifi_manager) and rebooting so the device joins the configured network. The
 * server listens on every active interface (SoftAP on real hardware, and the
 * emulated Ethernet interface under QEMU), so the same firmware is testable in
 * both places. Returns the server handle, or NULL on failure.
 */
httpd_handle_t http_config_server_start(void);

/* Stop a server previously started with http_config_server_start(). */
void http_config_server_stop(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
