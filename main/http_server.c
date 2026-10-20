#include "http_server.h"
#include "cyd_display.h"
#include "cyd_bmp.h"
#include "ha_client.h"
#include "cyd_cfg.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include <stdlib.h>

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

#define POST_BODY_MAXLEN 1024
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

    char reply[512];
    assistant_chat_reply(msg, reply, sizeof(reply));

    char reply_esc[900];
    wifi_form_json_escape(reply, reply_esc, sizeof(reply_esc));

    char out[940];
    snprintf(out, sizeof(out), "{\"reply\":\"%s\"}", reply_esc);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, out);
    return ESP_OK;
}


/* Params from the query string and/or a urlencoded body. */
static void get_param(httpd_req_t *req, const char *body, const char *key, char *out, size_t n)
{
    out[0] = '\0';
    char q[256];
    if (httpd_req_get_url_query_str(req, q, sizeof q) == ESP_OK &&
        httpd_query_key_value(q, key, out, n) == ESP_OK && out[0]) {
        return;
    }
    if (body && body[0]) wifi_form_get_field(body, key, out, n);
}

static bool key_ok(httpd_req_t *req, const char *body)
{
    char key[40], want[17];
    get_param(req, body, "key", key, sizeof key);
    ha_client_device_key(want);
    return want[0] && strcmp(key, want) == 0;
}

/* POST /camera?key=..&entity=camera.x&seconds=20 -> live snapshots on the panel. */
static esp_err_t camera_post_handler(httpd_req_t *req)
{
    char body[POST_BODY_MAXLEN] = {0};
    if (req->content_len > 0 && read_body(req, body, sizeof(body)) < 0) return ESP_FAIL;
    httpd_resp_set_type(req, "application/json");
    if (!key_ok(req, body)) {
        httpd_resp_set_status(req, "403 Forbidden");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"key\"}");
    }
    char ent[64], secs[8];
    get_param(req, body, "entity", ent, sizeof ent);
    get_param(req, body, "seconds", secs, sizeof secs);
    if (strncmp(ent, "camera.", 7) != 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"entity\"}");
    }
    cyd_display_show_camera(ent, atoi(secs));
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/* POST /display?key=..&invert=0|1&bgr=0|1&test=1
 * invert is applied on the next display_task pass; bgr after reboot. */
static esp_err_t display_post_handler(httpd_req_t *req)
{
    char body[POST_BODY_MAXLEN] = {0};
    if (req->content_len > 0 && read_body(req, body, sizeof(body)) < 0) return ESP_FAIL;
    httpd_resp_set_type(req, "application/json");
    if (!key_ok(req, body)) {
        httpd_resp_set_status(req, "403 Forbidden");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"key\"}");
    }
    char inv[4], bgr[4], test[4];
    get_param(req, body, "invert", inv, sizeof inv);
    get_param(req, body, "bgr", bgr, sizeof bgr);
    get_param(req, body, "test", test, sizeof test);
    cyd_display_set_color(inv[0] ? atoi(inv) : -1, bgr[0] ? atoi(bgr) : -1, test[0] == '1');
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}


/* ---- Alerts ---- */
static esp_err_t alert_post_handler(httpd_req_t *req)
{
    char body[POST_BODY_MAXLEN] = {0};
    if (req->content_len > 0 && read_body(req, body, sizeof(body)) < 0) return ESP_FAIL;
    httpd_resp_set_type(req, "application/json");
    if (!key_ok(req, body)) {
        httpd_resp_set_status(req, "403 Forbidden");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"key\"}");
    }
    char id[32], title[96], msg[192], lvl[12], dur[8], dis[32], clr[4];
    get_param(req, body, "id", id, sizeof id);
    get_param(req, body, "dismiss", dis, sizeof dis);
    get_param(req, body, "clear", clr, sizeof clr);
    if (clr[0] == '1') { cyd_alert_dismiss(NULL); return httpd_resp_sendstr(req, "{\"ok\":true}"); }
    if (dis[0]) { cyd_alert_dismiss(dis); return httpd_resp_sendstr(req, "{\"ok\":true}"); }
    get_param(req, body, "title", title, sizeof title);
    get_param(req, body, "message", msg, sizeof msg);
    get_param(req, body, "level", lvl, sizeof lvl);
    get_param(req, body, "duration", dur, sizeof dur);
    int level = (strcmp(lvl, "crit") == 0 || strcmp(lvl, "critical") == 0 || strcmp(lvl, "2") == 0) ? 2 :
                ((strcmp(lvl, "warn") == 0 || strcmp(lvl, "warning") == 0 || strcmp(lvl, "1") == 0) ? 1 : 0);
    if (!title[0] && !msg[0]) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"title/message\"}");
    }
    cyd_alert_push(id, title[0] ? title : "Home Assistant", msg, level, atoi(dur));
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static void alerts_json_fn(char *b, size_t n) { cyd_alerts_json(b, n); }
static void touch_json_fn(char *b, size_t n) { cyd_touch_json(b, n); }
static esp_err_t touch_get_handler(httpd_req_t *req) { return send_json(req, touch_json_fn, 256); }
static esp_err_t alerts_get_handler(httpd_req_t *req) { return send_json(req, alerts_json_fn, 1200); }

/* ---- Settings (same LAN trust as /hub) ---- */
static esp_err_t config_get_handler(httpd_req_t *req) { return send_json(req, cyd_cfg_json, 1024); }
static esp_err_t config_post_handler(httpd_req_t *req)
{
    char body[POST_BODY_MAXLEN] = {0};
    if (read_body(req, body, sizeof(body)) < 0) return ESP_FAIL;
    cyd_cfg_set_form(body);
    cyd_display_apply_cfg();
    return send_json(req, cyd_cfg_json, 1024);
}

/* ---- OTA: POST /ota?key=.. with the raw esp32.bin, or ?url=http://.../esp32.bin ---- */
static void reboot_soon(void *arg) { (void)arg; vTaskDelay(pdMS_TO_TICKS(1500)); esp_restart(); }

/* POST /reboot?key=.. -> restart in ~1.5 s (no unplugging after a freeze). */
static esp_err_t reboot_post_handler(httpd_req_t *req)
{
    char body[POST_BODY_MAXLEN] = {0};
    if (req->content_len > 0 && read_body(req, body, sizeof(body)) < 0) return ESP_FAIL;
    httpd_resp_set_type(req, "application/json");
    if (!key_ok(req, body)) {
        httpd_resp_set_status(req, "403 Forbidden");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"key\"}");
    }
    httpd_resp_sendstr(req, "{\"ok\":true,\"reboot\":true}");
    xTaskCreate(reboot_soon, "rb", 2048, NULL, 5, NULL);
    return ESP_OK;
}

/* GET /screenshot?key=.. -> chunked 24-bit BMP. The handler only enqueues;
 * display_task reads the glass one line at a time into a shared band. */
static esp_err_t screenshot_fail(httpd_req_t *req, const char *error)
{
    cyd_display_shot_end();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "503 Service Unavailable");
    char body[48];
    snprintf(body, sizeof body, "{\"ok\":false,\"error\":\"%s\"}", error);
    return httpd_resp_sendstr(req, body);
}

static esp_err_t screenshot_get_handler(httpd_req_t *req)
{
    if (!key_ok(req, NULL)) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "403 Forbidden");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"key\"}");
    }
    esp_err_t err = cyd_display_shot_begin();
    if (err != ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "503 Service Unavailable");
        const char *error = (err == ESP_ERR_INVALID_STATE) ? "busy" : "timeout";
        char body[48];
        snprintf(body, sizeof body, "{\"ok\":false,\"error\":\"%s\"}", error);
        return httpd_resp_sendstr(req, body);
    }

    const uint16_t *band = NULL;
    int rows = 0;
    /* 503 only before the body starts. After that the status is already 200. */
    if (cyd_display_shot_recv(&band, &rows, CYD_SHOT_TIMEOUT_MS) != 1) {
        return screenshot_fail(req, "timeout");
    }

    httpd_resp_set_type(req, "image/bmp");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    uint8_t hdr[CYD_BMP_HEADER];
    cyd_bmp_header(hdr);
    if (httpd_resp_send_chunk(req, (const char *)hdr, CYD_BMP_HEADER) != ESP_OK) {
        cyd_display_shot_end();
        return ESP_FAIL;
    }

    /* One BMP row, not a frame. The httpd task is the only writer. */
    static uint8_t line[CYD_BMP_ROW];
    int sent = CYD_BMP_HEADER;
    for (;;) {
        for (int r = 0; r < rows; r++) {
            cyd_bmp_row(line, band + (size_t)r * CYD_W, CYD_W);
            if (httpd_resp_send_chunk(req, (const char *)line, CYD_BMP_ROW) != ESP_OK) {
                cyd_display_shot_end();
                return ESP_FAIL;
            }
            sent += CYD_BMP_ROW;
        }
        cyd_display_shot_ack();
        if (sent >= CYD_BMP_SIZE) {
            /* Pixels are out. The trailing 0 from display_task is optional. */
            cyd_display_shot_recv(&band, &rows, 200);
            break;
        }
        int rc = cyd_display_shot_recv(&band, &rows, CYD_SHOT_TIMEOUT_MS);
        if (rc == 0) {
            break;
        }
        if (rc != 1) {
            cyd_display_shot_end();
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }
    }
    cyd_display_shot_end();
    ESP_LOGI(TAG, "screenshot %d bytes", sent);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t ota_post_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    if (!key_ok(req, NULL)) {
        httpd_resp_set_status(req, "403 Forbidden");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"key\"}");
    }
    char url[200];
    get_param(req, NULL, "url", url, sizeof url);
    if (url[0]) {
        esp_http_client_config_t hc = { .url = url, .timeout_ms = 15000, .keep_alive_enable = true };
        esp_https_ota_config_t oc = { .http_config = &hc };
        esp_err_t err = esp_https_ota(&oc);
        if (err != ESP_OK) {
            char m[96]; snprintf(m, sizeof m, "{\"ok\":false,\"error\":\"%s\"}", esp_err_to_name(err));
            return httpd_resp_sendstr(req, m);
        }
        httpd_resp_sendstr(req, "{\"ok\":true,\"reboot\":true}");
        xTaskCreate(reboot_soon, "ota_rb", 2048, NULL, 5, NULL);
        return ESP_OK;
    }
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part || req->content_len <= 0 || (size_t)req->content_len > part->size) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"size/partition\"}");
    }
    esp_ota_handle_t h;
    if (esp_ota_begin(part, req->content_len, &h) != ESP_OK) return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"begin\"}");
    char *buf = malloc(4096);
    int left = req->content_len, ok = buf != NULL;
    while (ok && left > 0) {
        int r = httpd_req_recv(req, buf, left < 4096 ? left : 4096);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0 || esp_ota_write(h, buf, r) != ESP_OK) { ok = 0; break; }
        left -= r;
    }
    free(buf);
    if (!ok) { esp_ota_abort(h); return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"write\"}"); }
    if (esp_ota_end(h) != ESP_OK || esp_ota_set_boot_partition(part) != ESP_OK)
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"image invalida\"}");
    httpd_resp_sendstr(req, "{\"ok\":true,\"reboot\":true}");
    xTaskCreate(reboot_soon, "ota_rb", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static esp_err_t hub_post_handler(httpd_req_t *req)
{
    char body[POST_BODY_MAXLEN];
    if (read_body(req, body, sizeof(body)) < 0) {
        return ESP_FAIL;
    }
    char en[8] = {0}, type[32] = {0}, addr[96] = {0}, user[64] = {0}, pass[ASSISTANT_TOKEN_MAX] = {0};
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
    /* Chat proxies to HA (TLS-free HTTP client + JSON buffers): needs more stack. */
    config.stack_size = 10240;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 24;

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
        { .uri = "/camera",       .method = HTTP_POST, .handler = camera_post_handler },
        { .uri = "/display",      .method = HTTP_POST, .handler = display_post_handler },
        { .uri = "/alert",        .method = HTTP_POST, .handler = alert_post_handler },
        { .uri = "/alerts",       .method = HTTP_GET,  .handler = alerts_get_handler },
        { .uri = "/touch",        .method = HTTP_GET,  .handler = touch_get_handler },
        { .uri = "/config",       .method = HTTP_GET,  .handler = config_get_handler },
        { .uri = "/config",       .method = HTTP_POST, .handler = config_post_handler },
        { .uri = "/ota",          .method = HTTP_POST, .handler = ota_post_handler },
        { .uri = "/reboot",       .method = HTTP_POST, .handler = reboot_post_handler },
        { .uri = "/screenshot",   .method = HTTP_GET,  .handler = screenshot_get_handler },
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
