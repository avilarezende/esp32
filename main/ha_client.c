#include "ha_client.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "assistant.h"
#include "wifi_manager.h"

static const char *TAG = "ha";

#define POLL_MS       30000
#define HTTP_BUF_MAX  2048
#define BODY_CAP      49152

static ha_snapshot_t s_snap;
static SemaphoreHandle_t s_lock;
static int s_started;

static void snap_lock(void)
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void snap_unlock(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

static void set_status(ha_snapshot_t *s, const char *st)
{
    snprintf(s->status, sizeof s->status, "%s", st);
}

static int json_str(const char *obj, const char *key, char *out, size_t out_len)
{
    char pat[48];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(obj, pat);
    if (!p) {
        return 0;
    }
    p = strchr(p + strlen(pat), ':');
    if (!p) {
        return 0;
    }
    p++;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '"') {
        return 0;
    }
    p++;
    size_t n = 0;
    while (*p && *p != '"' && n + 1 < out_len) {
        if (*p == '\\' && p[1]) {
            p++;
        }
        out[n++] = *p++;
    }
    out[n] = '\0';
    return n > 0;
}

static int json_num(const char *obj, const char *key, int *out)
{
    char pat[48];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(obj, pat);
    if (!p) {
        return 0;
    }
    p = strchr(p + strlen(pat), ':');
    if (!p) {
        return 0;
    }
    p++;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p) {
        return 0;
    }
    *out = (int)v;
    return 1;
}

static int looks_like(const char *id, const char *prefix)
{
    size_t n = strlen(prefix);
    return strncmp(id, prefix, n) == 0;
}

static int is_temp_entity(const char *id, const char *name)
{
    if (looks_like(id, "weather.") || looks_like(id, "climate.")) {
        return 1;
    }
    if (strstr(id, "temperature") || strstr(id, "temp")) {
        return 1;
    }
    if (name && (strstr(name, "emperatura") || strstr(name, "emp"))) {
        return 1;
    }
    return 0;
}

static int is_hum_entity(const char *id, const char *name)
{
    if (strstr(id, "humidity") || strstr(id, "umidade")) {
        return 1;
    }
    if (name && (strstr(name, "midade") || strstr(name, "umidity"))) {
        return 1;
    }
    return 0;
}

static int parse_int_state(const char *state, int *out)
{
    char *end = NULL;
    long v = strtol(state, &end, 10);
    if (end == state) {
        return 0;
    }
    *out = (int)v;
    return 1;
}

static void add_device(ha_snapshot_t *s, const char *name, const char *detail, int on)
{
    if (s->device_count >= HA_DEVICE_MAX || !name || !name[0]) {
        return;
    }
    ha_device_t *d = &s->devices[s->device_count++];
    snprintf(d->name, sizeof d->name, "%s", name);
    snprintf(d->detail, sizeof d->detail, "%s", detail ? detail : "");
    d->on = on;
}

static void ingest_entity(ha_snapshot_t *s, const char *obj)
{
    char entity_id[64] = {0};
    char state[32] = {0};
    char name[48] = {0};
    if (!json_str(obj, "entity_id", entity_id, sizeof entity_id)) {
        return;
    }
    json_str(obj, "state", state, sizeof state);
    if (!json_str(obj, "friendly_name", name, sizeof name)) {
        strncpy(name, entity_id, sizeof name - 1);
        name[sizeof name - 1] = '\0';
    }
    if (strcmp(state, "unavailable") == 0 || strcmp(state, "unknown") == 0) {
        return;
    }

    if (is_temp_entity(entity_id, name)) {
        int t = 0;
        if (json_num(obj, "temperature", &t) ||
            json_num(obj, "current_temperature", &t) ||
            parse_int_state(state, &t)) {
            if (!s->has_temp || looks_like(entity_id, "weather.") || looks_like(entity_id, "climate.")) {
                s->temp_c = t;
                s->has_temp = 1;
            }
        }
    }
    if (is_hum_entity(entity_id, name)) {
        int h = 0;
        if (json_num(obj, "humidity", &h) || parse_int_state(state, &h)) {
            if (h >= 0 && h <= 100) {
                s->humidity = h;
                s->has_humidity = 1;
            }
        }
    }

    if (looks_like(entity_id, "light.")) {
        s->lights_total++;
        if (strcmp(state, "on") == 0) {
            s->lights_on++;
            if (s->device_count < HA_DEVICE_MAX) {
                add_device(s, name, "ligada", 1);
            }
        }
        return;
    }
    if (looks_like(entity_id, "switch.") || looks_like(entity_id, "fan.") ||
        looks_like(entity_id, "media_player.") || looks_like(entity_id, "cover.") ||
        looks_like(entity_id, "lock.") || looks_like(entity_id, "binary_sensor.") ||
        looks_like(entity_id, "climate.")) {
        char detail[HA_DETAIL_MAX];
        int on = -1;
        if (strcmp(state, "on") == 0 || strcmp(state, "open") == 0 ||
            strcmp(state, "unlocked") == 0 || strcmp(state, "home") == 0) {
            on = 1;
            snprintf(detail, sizeof detail, "ligado");
        } else if (strcmp(state, "off") == 0 || strcmp(state, "closed") == 0 ||
                   strcmp(state, "locked") == 0 || strcmp(state, "away") == 0) {
            on = 0;
            snprintf(detail, sizeof detail, "deslig.");
        } else {
            strncpy(detail, state, sizeof detail - 1);
            detail[sizeof detail - 1] = '\0';
        }
        if (looks_like(entity_id, "climate.")) {
            int t = 0;
            if (json_num(obj, "current_temperature", &t) || json_num(obj, "temperature", &t)) {
                snprintf(detail, sizeof detail, "%dc", t);
            }
        }
        if (looks_like(entity_id, "lock.")) {
            snprintf(detail, sizeof detail, on == 1 ? "aberta" : (on == 0 ? "tranc." : state));
        }
        if (s->device_count < HA_DEVICE_MAX) {
            add_device(s, name, detail, on);
        }
    }
}

static void scan_body(ha_snapshot_t *s, const char *body, int len)
{
    int depth = 0;
    int obj_start = -1;
    for (int i = 0; i < len; i++) {
        char ch = body[i];
        if (ch == '{') {
            if (depth == 1) {
                obj_start = i;
            }
            depth++;
        } else if (ch == '}') {
            depth--;
            if (depth == 1 && obj_start >= 0) {
                int obj_len = i - obj_start + 1;
                if (obj_len > 32 && obj_len < 1800) {
                    char tmp[1800];
                    memcpy(tmp, body + obj_start, (size_t)obj_len);
                    tmp[obj_len] = '\0';
                    ingest_entity(s, tmp);
                }
                obj_start = -1;
            }
        }
    }
}

typedef struct {
    char *body;
    int len;
    int cap;
    int status;
} http_ctx_t;

static esp_err_t http_event(esp_http_client_event_t *evt)
{
    http_ctx_t *ctx = (http_ctx_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0 && ctx->body) {
        int room = ctx->cap - ctx->len - 1;
        int n = evt->data_len < room ? evt->data_len : room;
        if (n > 0) {
            memcpy(ctx->body + ctx->len, evt->data, (size_t)n);
            ctx->len += n;
            ctx->body[ctx->len] = '\0';
        }
    }
    return ESP_OK;
}

static void build_url(char *url, size_t url_len, const char *addr)
{
    if (strncmp(addr, "http://", 7) == 0 || strncmp(addr, "https://", 8) == 0) {
        snprintf(url, url_len, "%s/api/states", addr);
        return;
    }
    snprintf(url, url_len, "http://%s/api/states", addr);
}

static esp_err_t fetch_once(ha_snapshot_t *out)
{
    char type[ASSISTANT_STR_MAX] = {0};
    char addr[ASSISTANT_STR_MAX] = {0};
    char user[ASSISTANT_STR_MAX] = {0};
    char pass[ASSISTANT_STR_MAX] = {0};
    assistant_get_hub(type, sizeof type, addr, sizeof addr, user, sizeof user, pass, sizeof pass);
    (void)user;

    memset(out, 0, sizeof *out);
    snprintf(out->place, sizeof out->place, "Casa");
    if (!assistant_hub_configured()) {
        out->configured = 0;
        set_status(out, "sem hub");
        return ESP_ERR_INVALID_STATE;
    }
    out->configured = 1;
    if (type[0] && strcmp(type, "homeassistant") != 0) {
        set_status(out, "hub nao HA");
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!pass[0]) {
        set_status(out, "sem token");
        return ESP_ERR_INVALID_ARG;
    }

    char url[160];
    build_url(url, sizeof url, addr);

    http_ctx_t ctx = {0};
    ctx.cap = BODY_CAP;
    ctx.body = malloc((size_t)ctx.cap);
    if (!ctx.body) {
        set_status(out, "sem mem");
        return ESP_ERR_NO_MEM;
    }
    ctx.body[0] = '\0';

    char auth[ASSISTANT_STR_MAX + 24];
    snprintf(auth, sizeof auth, "Bearer %s", pass);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 8000,
        .event_handler = http_event,
        .user_data = &ctx,
        .buffer_size = HTTP_BUF_MAX,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(ctx.body);
        set_status(out, "http falhou");
        return ESP_FAIL;
    }
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "HA fetch failed err=%s status=%d bytes=%d",
                 esp_err_to_name(err), status, ctx.len);
        free(ctx.body);
        set_status(out, status == 401 ? "token ruim" : "offline");
        out->ready = -1;
        return err != ESP_OK ? err : ESP_FAIL;
    }

    scan_body(out, ctx.body, ctx.len);
    free(ctx.body);

    if (out->lights_total > 0 && out->device_count == 0) {
        char detail[HA_DETAIL_MAX];
        int on = out->lights_on > 9 ? 9 : out->lights_on;
        int tot = out->lights_total > 9 ? 9 : out->lights_total;
        snprintf(detail, sizeof detail, "%d/%d", on, tot);
        add_device(out, "Luzes", detail, out->lights_on > 0 ? 1 : 0);
    }
    out->ready = 1;
    set_status(out, "HA ok");
    ESP_LOGI(TAG, "HA ok: temp=%d hum=%d devices=%d lights=%d/%d",
             out->has_temp ? out->temp_c : -1,
             out->has_humidity ? out->humidity : -1,
             out->device_count, out->lights_on, out->lights_total);
    return ESP_OK;
}

static void ha_task(void *arg)
{
    (void)arg;
    /* First poll soon after STA has an IP. */
    vTaskDelay(pdMS_TO_TICKS(2000));
    while (1) {
        if (wifi_manager_is_connected()) {
            ha_snapshot_t next;
            esp_err_t err = fetch_once(&next);
            snap_lock();
            if (err == ESP_OK || next.configured) {
                s_snap = next;
            } else if (!s_snap.configured) {
                s_snap = next;
            } else {
                /* Keep last good devices; only refresh status. */
                snprintf(s_snap.status, sizeof s_snap.status, "%s", next.status);
                s_snap.ready = next.ready;
            }
            snap_unlock();
        } else {
            snap_lock();
            if (!assistant_hub_configured()) {
                memset(&s_snap, 0, sizeof s_snap);
                snprintf(s_snap.place, sizeof s_snap.place, "Casa");
                set_status(&s_snap, "sem hub");
            }
            snap_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

esp_err_t ha_client_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateMutex();
    memset(&s_snap, 0, sizeof s_snap);
    snprintf(s_snap.place, sizeof s_snap.place, "Casa");
    set_status(&s_snap, assistant_hub_configured() ? "aguardando" : "sem hub");
    s_snap.configured = assistant_hub_configured();
    if (xTaskCreate(ha_task, "ha", 8192, NULL, 3, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_started = 1;
    ESP_LOGI(TAG, "Home Assistant poller started");
    return ESP_OK;
}

void ha_client_copy(ha_snapshot_t *out)
{
    if (!out) {
        return;
    }
    snap_lock();
    *out = s_snap;
    snap_unlock();
}
