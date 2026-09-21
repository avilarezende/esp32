#include "http_server.h"

#include <string.h>
#include <stdlib.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"

#include "wifi_manager.h"

static const char *TAG = "http_config";

#define MAX_SCAN_APS       12
#define POST_BODY_MAXLEN   512

/* Decode an application/x-www-form-urlencoded token in place ('+' -> space,
 * %XX -> byte). `dst` may equal `src`. */
static void url_decode(const char *src, char *dst, size_t dst_size)
{
    size_t di = 0;
    for (size_t si = 0; src[si] != '\0' && di + 1 < dst_size; si++) {
        char c = src[si];
        if (c == '+') {
            dst[di++] = ' ';
        } else if (c == '%' && src[si + 1] && src[si + 2]) {
            char hex[3] = { src[si + 1], src[si + 2], '\0' };
            dst[di++] = (char)strtol(hex, NULL, 16);
            si += 2;
        } else {
            dst[di++] = c;
        }
    }
    dst[di] = '\0';
}

/* Build the configuration page, embedding a datalist of scanned SSIDs. */
static void render_index(httpd_req_t *req)
{
    uint16_t ap_count = 0;
#if CONFIG_APP_ENABLE_WIFI_RADIO
    wifi_ap_record_t ap_records[MAX_SCAN_APS] = {0};

    /* Best-effort scan of nearby networks to populate the SSID suggestions. */
    wifi_scan_config_t scan_cfg = { .show_hidden = false };
    if (esp_wifi_scan_start(&scan_cfg, true) == ESP_OK) {
        uint16_t num = MAX_SCAN_APS;
        esp_wifi_scan_get_ap_records(&num, ap_records);
        esp_wifi_scan_get_ap_num(&ap_count);
        if (ap_count > MAX_SCAN_APS) {
            ap_count = MAX_SCAN_APS;
        }
    } else {
        ESP_LOGD(TAG, "scan unavailable");
    }
#endif

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req,
        "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<title>ESP32 Wi-Fi Setup</title><style>"
        "body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;"
        "background:#0f172a;color:#e2e8f0;margin:0;display:flex;min-height:100vh;"
        "align-items:center;justify-content:center}"
        ".card{background:#1e293b;padding:28px 32px;border-radius:14px;width:320px;"
        "box-shadow:0 10px 30px rgba(0,0,0,.4)}"
        "h1{font-size:1.25rem;margin:0 0 4px}p{color:#94a3b8;font-size:.85rem;margin:0 0 18px}"
        "label{display:block;font-size:.8rem;margin:14px 0 6px;color:#cbd5e1}"
        "input{width:100%;box-sizing:border-box;padding:10px 12px;border-radius:8px;"
        "border:1px solid #334155;background:#0f172a;color:#e2e8f0;font-size:.95rem}"
        "button{width:100%;margin-top:22px;padding:11px;border:0;border-radius:8px;"
        "background:#3b82f6;color:#fff;font-size:1rem;font-weight:600;cursor:pointer}"
        "button:hover{background:#2563eb}</style></head><body><div class=\"card\">"
        "<h1>Wi-Fi Setup</h1><p>Choose a network for your ESP32 to join.</p>"
        "<form method=\"POST\" action=\"/connect\">"
        "<label for=\"ssid\">Network (SSID)</label>"
        "<input id=\"ssid\" name=\"ssid\" list=\"networks\" placeholder=\"Your Wi-Fi name\" required>"
        "<datalist id=\"networks\">");

#if CONFIG_APP_ENABLE_WIFI_RADIO
    for (uint16_t i = 0; i < ap_count; i++) {
        char opt[80];
        snprintf(opt, sizeof(opt), "<option value=\"%s\">", (char *)ap_records[i].ssid);
        httpd_resp_sendstr_chunk(req, opt);
    }
#else
    (void)ap_count;
#endif

    httpd_resp_sendstr_chunk(req,
        "</datalist>"
        "<label for=\"password\">Password</label>"
        "<input id=\"password\" name=\"password\" type=\"password\" placeholder=\"Leave blank if open\">"
        "<button type=\"submit\">Save &amp; Connect</button>"
        "</form></div></body></html>");
    httpd_resp_sendstr_chunk(req, NULL); /* end chunked response */
}

static esp_err_t index_get_handler(httpd_req_t *req)
{
    render_index(req);
    return ESP_OK;
}

#if CONFIG_APP_ENABLE_WIFI_RADIO
/* Deferred reboot so the HTTP response can be flushed to the client first. */
static void restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1500));
    ESP_LOGI(TAG, "restarting to apply new Wi-Fi credentials");
    esp_restart();
}
#endif

static esp_err_t connect_post_handler(httpd_req_t *req)
{
    char body[POST_BODY_MAXLEN];
    int received = 0;
    int remaining = MIN(req->content_len, (int)sizeof(body) - 1);
    while (remaining > 0) {
        int r = httpd_req_recv(req, body + received, remaining);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return ESP_FAIL;
        }
        received += r;
        remaining -= r;
    }
    body[received] = '\0';

    char ssid_raw[WIFI_MANAGER_SSID_MAXLEN * 3] = {0};
    char pass_raw[WIFI_MANAGER_PASS_MAXLEN * 3] = {0};
    char ssid[WIFI_MANAGER_SSID_MAXLEN] = {0};
    char pass[WIFI_MANAGER_PASS_MAXLEN] = {0};

    if (httpd_query_key_value(body, "ssid", ssid_raw, sizeof(ssid_raw)) != ESP_OK ||
        strlen(ssid_raw) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID is required");
        return ESP_OK;
    }
    /* Password is optional (open networks). */
    httpd_query_key_value(body, "password", pass_raw, sizeof(pass_raw));

    url_decode(ssid_raw, ssid, sizeof(ssid));
    url_decode(pass_raw, pass, sizeof(pass));

    esp_err_t err = wifi_manager_save_credentials(ssid, pass);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to store credentials: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save credentials");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "received new credentials via portal for SSID '%s'", ssid);

    char page[512];
    snprintf(page, sizeof(page),
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<title>Connecting</title></head><body style=\"font-family:sans-serif;"
        "background:#0f172a;color:#e2e8f0;text-align:center;padding-top:60px\">"
        "<h2>Saved.</h2><p>Connecting to <b>%s</b>&hellip;<br>The device will now restart.</p>"
        "</body></html>", ssid);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, page);

#if CONFIG_APP_ENABLE_WIFI_RADIO
    /* On real hardware, reboot so the device comes back up as a station and
     * joins the freshly configured network. */
    xTaskCreate(restart_task, "restart_task", 2048, NULL, 5, NULL);
#else
    /* QEMU build (no Wi-Fi radio): the credentials are now persisted in NVS.
     * Skip the automatic reboot, which under QEMU would re-enter the portal
     * (and cannot join a network anyway). Power-cycle the emulator to see the
     * device pick up the stored credentials on the next boot. */
    ESP_LOGI(TAG, "credentials saved to NVS; automatic restart skipped in QEMU build");
#endif
    return ESP_OK;
}

httpd_handle_t http_config_server_start(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    if (httpd_start(&server, &config) != ESP_OK) {
        return NULL;
    }

    static const httpd_uri_t index_uri = {
        .uri = "/", .method = HTTP_GET, .handler = index_get_handler,
    };
    static const httpd_uri_t connect_uri = {
        .uri = "/connect", .method = HTTP_POST, .handler = connect_post_handler,
    };
    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &connect_uri);

    ESP_LOGI(TAG, "configuration web server started on port %d", config.server_port);
    return server;
}

void http_config_server_stop(httpd_handle_t server)
{
    if (server) {
        httpd_stop(server);
    }
}
