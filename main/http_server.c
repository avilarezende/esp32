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
#include "assistant.h"

static const char *TAG = "http_config";

#define POST_BODY_MAXLEN 512
#define SCAN_JSON_MAXLEN 1024

/* Touch-sized portal: 48px targets, 16px type, card fits a 320px-wide panel. */
static const char PAGE_HEAD[] =
    "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<title>ESP32 Wi-Fi</title><style>"
    "*{box-sizing:border-box}"
    "body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;"
    "background:#0f172a;color:#e2e8f0;margin:0;min-height:100vh;display:flex;"
    "align-items:flex-start;justify-content:center;padding:12px}"
    ".card{background:#1e293b;padding:16px;border-radius:14px;width:min(400px,100%);"
    "box-shadow:0 10px 30px rgba(0,0,0,.4)}"
    "h1{font-size:1.25rem;margin:0 0 4px}p{color:#d6e0ee;font-size:.95rem;margin:0 0 12px}"
    "label{display:block;font-size:.9rem;margin:12px 0 6px;color:#e2e8f0}"
    "input,button,.net{min-height:48px;font-size:16px}"
    "input{width:100%;padding:12px;border-radius:10px;border:1px solid #334155;"
    "background:#0f172a;color:#e2e8f0}"
    "button{width:100%;margin-top:16px;padding:12px 14px;border:0;border-radius:10px;"
    "background:#1d4ed8;color:#fff;font-weight:700;cursor:pointer}"
    "button:active{background:#1e40af}button.alt{background:#334155}button.alt:active{background:#1e293b}"
    "ul{list-style:none;padding:0;margin:8px 0 0}li{margin:0 0 8px}"
    ".net{width:100%;text-align:left;background:#0f172a;border:1px solid #334155;"
    "border-radius:10px;padding:12px;color:#e2e8f0;cursor:pointer;margin:0}"
    ".net:active{border-color:#60a5fa}.row{display:flex;justify-content:space-between;align-items:center}"
    ".muted{color:#d6e0ee;font-size:.9rem}.warn{color:#fecaca}"
    "a.muted{min-height:44px;display:inline-flex;align-items:center}"
    "</style></head><body><div class=\"card\">";

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

static void send_config_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
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

/* ---- assistant web app (served once connected) ---- */

/* Touch UI lives in app.html (embedded). Symbol names come from EMBED_TXTFILES. */
extern const char app_html_start[] asm("_binary_app_html_start");

static void send_app_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, app_html_start, strlen(app_html_start));
}


/* Build a JSON document with `fn` and send it. */
static esp_err_t send_json(httpd_req_t *req, void (*fn)(char *, size_t), size_t cap)
{
    char *buf = malloc(cap);
    if (!buf) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    }
    fn(buf, cap);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    free(buf);
    return ESP_OK;
}

/* ---- handlers ---- */

static esp_err_t index_get_handler(httpd_req_t *req)
{
    if (wifi_manager_is_connected()) {
        send_app_page(req);
    } else {
        send_config_page(req);
    }
    return ESP_OK;
}

static esp_err_t state_get_handler(httpd_req_t *req)   { return send_json(req, assistant_state_json, 512); }
static esp_err_t bots_get_handler(httpd_req_t *req)    { return send_json(req, assistant_bots_json, 384); }
static esp_err_t devices_get_handler(httpd_req_t *req) { return send_json(req, assistant_devices_json, 1024); }
static esp_err_t discover_get_handler(httpd_req_t *req){ return send_json(req, assistant_discover_json, 256); }
static esp_err_t weather_get_handler(httpd_req_t *req) { return send_json(req, assistant_weather_json, 192); }

static esp_err_t chat_post_handler(httpd_req_t *req)
{
    char body[POST_BODY_MAXLEN];
    if (read_body(req, body, sizeof(body)) < 0) {
        return ESP_FAIL;
    }
    char msg[192] = {0};
    wifi_form_get_field(body, "message", msg, sizeof(msg));

    char reply[256];
    assistant_chat_reply(msg, reply, sizeof(reply));

    char reply_esc[400];
    wifi_form_json_escape(reply, reply_esc, sizeof(reply_esc));

    char out[440];
    snprintf(out, sizeof(out), "{\"reply\":\"%s\"}", reply_esc);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, out);
    return ESP_OK;
}

static esp_err_t hub_post_handler(httpd_req_t *req)
{
    char body[POST_BODY_MAXLEN];
    if (read_body(req, body, sizeof(body)) < 0) {
        return ESP_FAIL;
    }
    char en[8] = {0}, type[32] = {0}, addr[96] = {0}, user[64] = {0}, pass[96] = {0};
    wifi_form_get_field(body, "enabled", en, sizeof(en));
    wifi_form_get_field(body, "type", type, sizeof(type));
    wifi_form_get_field(body, "address", addr, sizeof(addr));
    wifi_form_get_field(body, "user", user, sizeof(user));
    wifi_form_get_field(body, "password", pass, sizeof(pass));

    bool enabled = (en[0] == '1');
    assistant_set_hub(enabled, type, addr, user, pass);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t bot_post_handler(httpd_req_t *req)
{
    char body[POST_BODY_MAXLEN];
    if (read_body(req, body, sizeof(body)) < 0) {
        return ESP_FAIL;
    }
    char id[32] = {0};
    wifi_form_get_field(body, "id", id, sizeof(id));
    assistant_set_principal_bot(id);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
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
    config.max_uri_handlers = 16;

    if (httpd_start(&server, &config) != ESP_OK) {
        return NULL;
    }

    const httpd_uri_t uris[] = {
        { .uri = "/",             .method = HTTP_GET,  .handler = index_get_handler },
        { .uri = "/scan",         .method = HTTP_GET,  .handler = scan_get_handler },
        { .uri = "/connect",      .method = HTTP_POST, .handler = connect_post_handler },
        { .uri = "/forget",       .method = HTTP_POST, .handler = forget_post_handler },
        { .uri = "/state",        .method = HTTP_GET,  .handler = state_get_handler },
        { .uri = "/bots",         .method = HTTP_GET,  .handler = bots_get_handler },
        { .uri = "/devices",      .method = HTTP_GET,  .handler = devices_get_handler },
        { .uri = "/hub/discover", .method = HTTP_GET,  .handler = discover_get_handler },
        { .uri = "/weather",      .method = HTTP_GET,  .handler = weather_get_handler },
        { .uri = "/chat",         .method = HTTP_POST, .handler = chat_post_handler },
        { .uri = "/hub",          .method = HTTP_POST, .handler = hub_post_handler },
        { .uri = "/bot",          .method = HTTP_POST, .handler = bot_post_handler },
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
