#include "http_server.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"

#include "wifi_manager.h"
#include "wifi_form.h"

static const char *TAG = "http_config";

#define POST_BODY_MAXLEN 512
#define SCAN_JSON_MAXLEN 1024

static const char PAGE_HEAD[] =
    "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<title>ESP32 Wi-Fi</title><style>"
    "body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;"
    "background:#0f172a;color:#e2e8f0;margin:0;display:flex;min-height:100vh;"
    "align-items:center;justify-content:center}"
    ".card{background:#1e293b;padding:28px 32px;border-radius:14px;width:340px;"
    "box-shadow:0 10px 30px rgba(0,0,0,.4)}"
    "h1{font-size:1.25rem;margin:0 0 4px}p{color:#94a3b8;font-size:.85rem;margin:0 0 14px}"
    "label{display:block;font-size:.8rem;margin:14px 0 6px;color:#cbd5e1}"
    "input{width:100%;box-sizing:border-box;padding:10px 12px;border-radius:8px;"
    "border:1px solid #334155;background:#0f172a;color:#e2e8f0;font-size:.95rem}"
    "button{width:100%;margin-top:18px;padding:11px;border:0;border-radius:8px;"
    "background:#3b82f6;color:#fff;font-size:1rem;font-weight:600;cursor:pointer}"
    "button:hover{background:#2563eb}button.alt{background:#475569}button.alt:hover{background:#334155}"
    "ul{list-style:none;padding:0;margin:8px 0 0}li{margin:0 0 6px}"
    ".net{width:100%;text-align:left;background:#0f172a;border:1px solid #334155;"
    "border-radius:8px;padding:8px 10px;color:#e2e8f0;cursor:pointer;font-size:.9rem;margin:0}"
    ".net:hover{border-color:#3b82f6}.row{display:flex;justify-content:space-between}"
    ".muted{color:#94a3b8;font-size:.8rem}.warn{color:#fca5a5}</style></head><body><div class=\"card\">";

static const char PAGE_TAIL[] = "</div></body></html>";

/* ---- helpers ---- */

static int read_body(httpd_req_t *req, char *buf, size_t buf_size)
{
    int received = 0;
    int remaining = MIN(req->content_len, (int)buf_size - 1);
    while (remaining > 0) {
        int r = httpd_req_recv(req, buf + received, remaining);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
            return -1;
        }
        received += r;
        remaining -= r;
    }
    buf[received] = '\0';
    return received;
}

#if CONFIG_APP_ENABLE_WIFI_RADIO
/* Deferred reboot so the HTTP response can be flushed to the client first. */
static void restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1500));
    ESP_LOGI(TAG, "restarting to apply Wi-Fi change");
    esp_restart();
}
static void schedule_restart(void)
{
    xTaskCreate(restart_task, "restart_task", 2048, NULL, 5, NULL);
}
#endif

/* ---- pages ---- */

static void send_status_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, PAGE_HEAD);
    httpd_resp_sendstr_chunk(req, "<h1>Connected</h1><p>This ESP32 is configured.</p>");

    char row[160];
    const char *ssid = wifi_manager_get_ssid();
    const char *ip = wifi_manager_get_ip();
    int rssi = wifi_manager_get_rssi();

    snprintf(row, sizeof(row), "<div class=\"row\"><span class=\"muted\">Network</span><span>%s</span></div>",
             (ssid && ssid[0]) ? ssid : "&mdash;");
    httpd_resp_sendstr_chunk(req, row);
    snprintf(row, sizeof(row), "<div class=\"row\"><span class=\"muted\">IP address</span><span>%s</span></div>",
             (ip && ip[0]) ? ip : "&mdash;");
    httpd_resp_sendstr_chunk(req, row);
    if (rssi != 0) {
        snprintf(row, sizeof(row), "<div class=\"row\"><span class=\"muted\">Signal</span><span>%d dBm</span></div>", rssi);
        httpd_resp_sendstr_chunk(req, row);
    }
    if (wifi_manager_get_state() == WIFI_MANAGER_STATE_CONNECTING) {
        httpd_resp_sendstr_chunk(req, "<p class=\"warn\">Reconnecting&hellip;</p>");
    }

    httpd_resp_sendstr_chunk(req,
        "<form method=\"POST\" action=\"/forget\">"
        "<button class=\"alt\" type=\"submit\">Forget network</button></form>");
    httpd_resp_sendstr_chunk(req, PAGE_TAIL);
    httpd_resp_sendstr_chunk(req, NULL);
}

static void send_config_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, PAGE_HEAD);
    httpd_resp_sendstr_chunk(req,
        "<h1>Wi-Fi Setup</h1><p>Choose a network for your ESP32 to join.</p>"
        "<form method=\"POST\" action=\"/connect\">"
        "<label for=\"ssid\">Network (SSID)</label>"
        "<input id=\"ssid\" name=\"ssid\" list=\"networks\" placeholder=\"Your Wi-Fi name\" "
        "maxlength=\"32\" required>"
        "<datalist id=\"networks\"></datalist>"
        "<label for=\"password\">Password</label>"
        "<input id=\"password\" name=\"password\" type=\"password\" placeholder=\"Leave blank if open\" "
        "maxlength=\"63\">"
        "<button type=\"submit\">Save &amp; Connect</button></form>"
        "<div class=\"row\" style=\"margin-top:18px\"><span class=\"muted\">Available networks</span>"
        "<a href=\"#\" class=\"muted\" onclick=\"loadNets();return false\">Rescan</a></div>"
        "<ul id=\"nets\"><li class=\"muted\">Scanning&hellip;</li></ul>"
        "<script>"
        "function pick(s){document.getElementById('ssid').value=s;"
        "document.getElementById('password').focus();}"
        "function loadNets(){var u=document.getElementById('nets');"
        "u.innerHTML='<li class=\"muted\">Scanning\\u2026</li>';"
        "fetch('/scan').then(function(r){return r.json()}).then(function(list){"
        "var dl=document.getElementById('networks');dl.innerHTML='';u.innerHTML='';"
        "if(!list.length){u.innerHTML='<li class=\"muted\">No networks found</li>';return;}"
        "list.forEach(function(n){"
        "var o=document.createElement('option');o.value=n.ssid;dl.appendChild(o);"
        "var li=document.createElement('li');var b=document.createElement('button');"
        "b.type='button';b.className='net';b.textContent=n.ssid+'  ('+n.rssi+' dBm)';"
        "b.onclick=function(){pick(n.ssid)};li.appendChild(b);u.appendChild(li);});"
        "}).catch(function(){u.innerHTML='<li class=\"warn\">Scan failed</li>';});}"
        "loadNets();"
        "</script>");
    httpd_resp_sendstr_chunk(req, PAGE_TAIL);
    httpd_resp_sendstr_chunk(req, NULL);
}

/* ---- handlers ---- */

static esp_err_t index_get_handler(httpd_req_t *req)
{
    if (wifi_manager_is_connected()) {
        send_status_page(req);
    } else {
        send_config_page(req);
    }
    return ESP_OK;
}

static esp_err_t scan_get_handler(httpd_req_t *req)
{
    char *json = malloc(SCAN_JSON_MAXLEN);
    if (!json) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    }
    wifi_manager_scan_json(json, SCAN_JSON_MAXLEN);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

static esp_err_t connect_post_handler(httpd_req_t *req)
{
    char body[POST_BODY_MAXLEN];
    if (read_body(req, body, sizeof(body)) < 0) {
        return ESP_FAIL;
    }

    char ssid[WIFI_MANAGER_SSID_MAXLEN] = {0};
    char pass[WIFI_MANAGER_PASS_MAXLEN] = {0};
    wifi_form_get_field(body, "ssid", ssid, sizeof(ssid));
    wifi_form_get_field(body, "password", pass, sizeof(pass));

    if (!wifi_form_valid_ssid(ssid)) {
        ESP_LOGW(TAG, "rejected invalid SSID");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID must be 1-32 characters");
        return ESP_OK;
    }
    if (!wifi_form_valid_password(pass)) {
        ESP_LOGW(TAG, "rejected invalid password length");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Password must be empty or 8-63 characters");
        return ESP_OK;
    }

    esp_err_t err = wifi_manager_save_credentials(ssid, pass);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save credentials");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "received new credentials via portal for SSID '%s'", ssid);

    char mid[160];
    snprintf(mid, sizeof(mid),
        "<h1>Saved</h1><p>Connecting to <b>%s</b>&hellip;</p>"
        "<p class=\"muted\">The device is applying the new configuration.</p>", ssid);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, PAGE_HEAD);
    httpd_resp_sendstr_chunk(req, mid);
    httpd_resp_sendstr_chunk(req, PAGE_TAIL);
    httpd_resp_sendstr_chunk(req, NULL);

#if CONFIG_APP_ENABLE_WIFI_RADIO
    schedule_restart();
#else
    ESP_LOGI(TAG, "credentials saved; simulating connected state (QEMU build)");
    wifi_manager_mark_connected(ssid);
#endif
    return ESP_OK;
}

static esp_err_t forget_post_handler(httpd_req_t *req)
{
    esp_err_t err = wifi_manager_forget();
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not clear credentials");
        return ESP_OK;
    }
    ESP_LOGW(TAG, "network forgotten via portal");

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, PAGE_HEAD);
    httpd_resp_sendstr_chunk(req,
        "<h1>Network forgotten</h1>"
        "<p>Stored credentials were cleared. The setup portal is available again.</p>");
    httpd_resp_sendstr_chunk(req, PAGE_TAIL);
    httpd_resp_sendstr_chunk(req, NULL);

#if CONFIG_APP_ENABLE_WIFI_RADIO
    schedule_restart();
#else
    wifi_manager_mark_provisioning();
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

    const httpd_uri_t uris[] = {
        { .uri = "/",        .method = HTTP_GET,  .handler = index_get_handler },
        { .uri = "/scan",    .method = HTTP_GET,  .handler = scan_get_handler },
        { .uri = "/connect", .method = HTTP_POST, .handler = connect_post_handler },
        { .uri = "/forget",  .method = HTTP_POST, .handler = forget_post_handler },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }

    ESP_LOGI(TAG, "configuration web server started on port %d", config.server_port);
    return server;
}

void http_config_server_stop(httpd_handle_t server)
{
    if (server) {
        httpd_stop(server);
    }
}
