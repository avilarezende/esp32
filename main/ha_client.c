#include "ha_client.h"

#include <ctype.h>
#include <math.h>
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

__attribute__((unused)) static void scan_body(ha_snapshot_t *s, const char *body, int len)
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

__attribute__((unused)) static void build_url(char *url, size_t url_len, const char *addr)
{
    if (strncmp(addr, "http://", 7) == 0 || strncmp(addr, "https://", 8) == 0) {
        snprintf(url, url_len, "%s/api/states", addr);
        return;
    }
    snprintf(url, url_len, "http://%s/api/states", addr);
}

/* Ask HA to render a tiny summary instead of downloading /api/states (which
 * is hundreds of KB on a real install and was truncated at 48 KB):
 *   T|<temp>|<humidity>   L|<lights on>|<lights total>   D|<name>|<domain>|<state> (x4) */
static const char HA_SUMMARY_BODY[] = "{\"template\": \"{%- set ts = states.sensor|selectattr('attributes.device_class','eq','temperature')|rejectattr('state','in',['unknown','unavailable'])|list -%}\\n{%- set hs = states.sensor|selectattr('attributes.device_class','eq','humidity')|rejectattr('state','in',['unknown','unavailable'])|list -%}\\n{%- set w = states.weather|list -%}\\nT|{{ (w[0].attributes.temperature if w and w[0].attributes.temperature is number else (ts[0].state if ts else '')) }}|{{ (w[0].attributes.humidity if w and w[0].attributes.humidity is number else (hs[0].state if hs else '')) }}\\n{%- set ds = (states.light|list + states.switch|list)|rejectattr('state','in',['unknown','unavailable'])|sort(attribute='state',reverse=true)|list %}\\nL|{{ states.light|selectattr('state','eq','on')|list|count }}|{{ states.light|rejectattr('state','in',['unknown','unavailable'])|list|count }}\\n{%- for d in ds[:4] %}\\nD|{{ d.name[:17]|replace('|',' ') }}|{{ d.domain }}|{{ d.state }}\\n{%- endfor %}\\n\"}";

static void parse_summary(ha_snapshot_t *s, char *body)
{
    char *save = NULL;
    for (char *line = strtok_r(body, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *f[4] = {0};
        int n = 0;
        char *sv2 = NULL;
        for (char *p = strtok_r(line, "|", &sv2); p && n < 4; p = strtok_r(NULL, "|", &sv2)) f[n++] = p;
        if (n >= 2 && strcmp(f[0], "T") == 0) {
            if (f[1] && f[1][0] && (isdigit((unsigned char)f[1][0]) || f[1][0] == '-')) { s->temp_c = (int)lroundf(strtof(f[1], NULL)); s->has_temp = 1; }
            if (n >= 3 && f[2][0] && isdigit((unsigned char)f[2][0])) { s->humidity = (int)lroundf(strtof(f[2], NULL)); s->has_humidity = 1; }
        } else if (n >= 3 && strcmp(f[0], "L") == 0) {
            s->lights_on = atoi(f[1]); s->lights_total = atoi(f[2]);
        } else if (n >= 4 && strcmp(f[0], "D") == 0) {
            int on = strcmp(f[3], "on") == 0 ? 1 : (strcmp(f[3], "off") == 0 ? 0 : -1);
            add_device(s, f[1], on == 1 ? "ligado" : (on == 0 ? "desligado" : f[3]), on);
        }
    }
}

static esp_err_t fetch_once(ha_snapshot_t *out)
{
    char type[ASSISTANT_STR_MAX] = {0};
    char addr[ASSISTANT_STR_MAX] = {0};
    static char pass[ASSISTANT_TOKEN_MAX];
    static char auth[ASSISTANT_TOKEN_MAX + 24];
    pass[0] = '\0';
    assistant_get_hub(type, sizeof type, addr, sizeof addr, NULL, 0, pass, sizeof pass);

    memset(out, 0, sizeof *out);
    snprintf(out->place, sizeof out->place, "Casa");
    if (!assistant_hub_configured()) { out->configured = 0; set_status(out, "sem hub"); return ESP_ERR_INVALID_STATE; }
    out->configured = 1;
    if (type[0] && strcmp(type, "homeassistant") != 0) { set_status(out, "hub nao HA"); return ESP_ERR_NOT_SUPPORTED; }
    if (!pass[0]) { set_status(out, "sem token"); return ESP_ERR_INVALID_ARG; }

    char url[160];
    if (strncmp(addr, "http://", 7) == 0 || strncmp(addr, "https://", 8) == 0) snprintf(url, sizeof url, "%s/api/template", addr);
    else snprintf(url, sizeof url, "http://%s/api/template", addr);

    http_ctx_t ctx = {0};
    ctx.cap = 2048;
    ctx.body = calloc(1, (size_t)ctx.cap);
    if (!ctx.body) { set_status(out, "sem mem"); return ESP_ERR_NO_MEM; }
    snprintf(auth, sizeof auth, "Bearer %s", pass);

    esp_http_client_config_t cfg = {
        .url = url, .method = HTTP_METHOD_POST, .timeout_ms = 8000,
        .event_handler = http_event, .user_data = &ctx,
        .buffer_size = HTTP_BUF_MAX, .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { free(ctx.body); set_status(out, "offline"); return ESP_FAIL; }
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, HA_SUMMARY_BODY, (int)strlen(HA_SUMMARY_BODY));
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "HA template failed err=%s status=%d", esp_err_to_name(err), status);
        free(ctx.body);
        set_status(out, status == 401 ? "token ruim" : "offline");
        out->ready = -1;
        return err != ESP_OK ? err : ESP_FAIL;
    }
    ctx.body[ctx.len] = '\0';
    parse_summary(out, ctx.body);
    free(ctx.body);
    out->ready = 1;
    set_status(out, "HA ok");
    ESP_LOGI(TAG, "HA ok: temp=%d hum=%d devices=%d lights=%d/%d",
             out->has_temp ? out->temp_c : -1, out->has_humidity ? out->humidity : -1,
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


/* ---- Conversation (chat) ---- */

#define CONV_BODY_CAP 4096
static char s_conv_id[64];

static void json_escape_into(const char *in, char *out, size_t out_len)
{
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 7 < out_len; p++) {
        if (*p == '"' || *p == '\\') { out[o++] = '\\'; out[o++] = (char)*p; }
        else if (*p == '\n') { out[o++] = '\\'; out[o++] = 'n'; }
        else if (*p == '\r' || *p == '\t') { out[o++] = ' '; }
        else if (*p < 0x20) { o += (size_t)snprintf(out + o, out_len - o, "\\u%04x", *p); }
        else out[o++] = (char)*p;
    }
    out[o] = '\0';
}

static int hexv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Decode the JSON string that starts right after the opening quote at `p`. */
static void json_unescape(const char *p, char *out, size_t out_len)
{
    size_t o = 0;
    while (*p && *p != '"' && o + 4 < out_len) {
        if (*p != '\\') { out[o++] = *p++; continue; }
        p++;
        switch (*p) {
        case 'n': out[o++] = '\n'; p++; break;
        case 't': out[o++] = ' '; p++; break;
        case 'r': p++; break;
        case 'u': {
            unsigned cp = 0; int ok = 1;
            for (int i = 1; i <= 4; i++) { int v = hexv(p[i]); if (v < 0) { ok = 0; break; } cp = cp * 16 + (unsigned)v; }
            if (!ok) { p++; break; }
            p += 5;
            if (cp >= 0xD800 && cp <= 0xDBFF && p[0] == '\\' && p[1] == 'u') {
                unsigned lo = 0; int ok2 = 1;
                for (int i = 2; i <= 5; i++) { int v = hexv(p[i]); if (v < 0) { ok2 = 0; break; } lo = lo * 16 + (unsigned)v; }
                if (ok2 && lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); p += 6; }
            }
            if (cp < 0x80) out[o++] = (char)cp;
            else if (cp < 0x800) { out[o++] = (char)(0xC0 | (cp >> 6)); out[o++] = (char)(0x80 | (cp & 0x3F)); }
            else if (cp < 0x10000) { out[o++] = (char)(0xE0 | (cp >> 12)); out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[o++] = (char)(0x80 | (cp & 0x3F)); }
            else if (o + 5 < out_len) { out[o++] = (char)(0xF0 | (cp >> 18)); out[o++] = (char)(0x80 | ((cp >> 12) & 0x3F)); out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[o++] = (char)(0x80 | (cp & 0x3F)); }
            break;
        }
        case '\0': break;
        default: out[o++] = *p++; break;
        }
    }
    out[o] = '\0';
}

void ha_client_conversation(const char *text, char *out, size_t out_len)
{
    if (!out || !out_len) return;
    out[0] = '\0';
    if (!text || !text[0]) { snprintf(out, out_len, "Escreva uma mensagem."); return; }

    char type[ASSISTANT_STR_MAX] = {0}, addr[ASSISTANT_STR_MAX] = {0};
    char *pass = calloc(1, ASSISTANT_TOKEN_MAX);
    char *auth = calloc(1, ASSISTANT_TOKEN_MAX + 24);
    char *req = calloc(1, 768);
    http_ctx_t ctx = {0};
    ctx.cap = CONV_BODY_CAP;
    ctx.body = calloc(1, CONV_BODY_CAP);
    if (!pass || !auth || !req || !ctx.body) { snprintf(out, out_len, "Sem memoria para falar com o Home Assistant."); goto done; }

    assistant_get_hub(type, sizeof type, addr, sizeof addr, NULL, 0, pass, ASSISTANT_TOKEN_MAX);
    if (!assistant_hub_configured()) { snprintf(out, out_len, "Configure o Home Assistant em Ajustes para conversar."); goto done; }
    if (type[0] && strcmp(type, "homeassistant") != 0) { snprintf(out, out_len, "O chat precisa de um hub Home Assistant."); goto done; }
    if (!pass[0]) { snprintf(out, out_len, "Falta o token do Home Assistant em Ajustes."); goto done; }

    char url[160];
    if (strncmp(addr, "http://", 7) == 0 || strncmp(addr, "https://", 8) == 0) snprintf(url, sizeof url, "%s/api/conversation/process", addr);
    else snprintf(url, sizeof url, "http://%s/api/conversation/process", addr);

    char esc[400];
    json_escape_into(text, esc, sizeof esc);
    int n = snprintf(req, 768, "{\"text\":\"%s\",\"language\":\"pt-BR\"", esc);
    if (s_conv_id[0]) n += snprintf(req + n, 768 - (size_t)n, ",\"conversation_id\":\"%s\"", s_conv_id);
    snprintf(req + n, 768 - (size_t)n, "}");
    snprintf(auth, ASSISTANT_TOKEN_MAX + 24, "Bearer %s", pass);

    esp_http_client_config_t cfg = {
        .url = url, .method = HTTP_METHOD_POST, .timeout_ms = 20000,
        .event_handler = http_event, .user_data = &ctx,
        .buffer_size = HTTP_BUF_MAX, .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { snprintf(out, out_len, "Falha ao iniciar HTTP."); goto done; }
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, req, (int)strlen(req));
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "conversation failed err=%s status=%d", esp_err_to_name(err), status);
        if (status == 401) snprintf(out, out_len, "Token do Home Assistant recusado (401).");
        else snprintf(out, out_len, "Home Assistant indisponivel (%d).", status);
        goto done;
    }
    ctx.body[ctx.len] = '\0';
    const char *sp = strstr(ctx.body, "\"plain\"");
    if (sp) sp = strstr(sp, "\"speech\"");
    if (sp) sp = strchr(sp + 8, '"');
    if (sp) json_unescape(sp + 1, out, out_len);
    if (!out[0]) snprintf(out, out_len, "O Home Assistant nao respondeu nada.");
    const char *cid = strstr(ctx.body, "\"conversation_id\"");
    if (cid) { cid = strchr(cid + 17, '"'); if (cid) json_unescape(cid + 1, s_conv_id, sizeof s_conv_id); }
    else s_conv_id[0] = '\0';
done:
    free(pass); free(auth); free(req); free(ctx.body);
}


/* ---- Camera snapshots / device key ---- */

#include "mbedtls/sha256.h"

void ha_client_device_key(char out[17])
{
    out[0] = '\0';
    char *pass = calloc(1, ASSISTANT_TOKEN_MAX);
    if (!pass) return;
    assistant_get_hub(NULL, 0, NULL, 0, NULL, 0, pass, ASSISTANT_TOKEN_MAX);
    if (pass[0]) {
        unsigned char d[32];
        mbedtls_sha256((const unsigned char *)pass, strlen(pass), d, 0);
        for (int i = 0; i < 8; i++) snprintf(out + i * 2, 3, "%02x", d[i]);
    }
    free(pass);
}

esp_err_t ha_client_fetch_camera(const char *entity, uint8_t **jpg, int *len)
{
    *jpg = NULL; *len = 0;
    char type[ASSISTANT_STR_MAX] = {0}, addr[ASSISTANT_STR_MAX] = {0};
    char *pass = calloc(1, ASSISTANT_TOKEN_MAX);
    char *auth = calloc(1, ASSISTANT_TOKEN_MAX + 24);
    http_ctx_t ctx = {0};
    ctx.cap = 40 * 1024;
    ctx.body = malloc((size_t)ctx.cap);
    esp_err_t ret = ESP_FAIL;
    if (!pass || !auth || !ctx.body) { ret = ESP_ERR_NO_MEM; goto out; }
    assistant_get_hub(type, sizeof type, addr, sizeof addr, NULL, 0, pass, ASSISTANT_TOKEN_MAX);
    if (!pass[0] || !addr[0]) { ret = ESP_ERR_INVALID_STATE; goto out; }
    char url[192];
    const char *pre = (strncmp(addr, "http://", 7) == 0 || strncmp(addr, "https://", 8) == 0) ? "" : "http://";
    snprintf(url, sizeof url, "%s%s/api/camera_proxy/%s?width=320&height=240", pre, addr, entity);
    snprintf(auth, ASSISTANT_TOKEN_MAX + 24, "Bearer %s", pass);
    esp_http_client_config_t cfg = {
        .url = url, .method = HTTP_METHOD_GET, .timeout_ms = 5000,
        .event_handler = http_event, .user_data = &ctx,
        .buffer_size = HTTP_BUF_MAX, .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) goto out;
    esp_http_client_set_header(client, "Authorization", auth);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || status != 200 || ctx.len < 4) {
        ESP_LOGW(TAG, "camera %s failed err=%s status=%d", entity, esp_err_to_name(err), status);
        goto out;
    }
    *jpg = (uint8_t *)ctx.body; *len = ctx.len; ctx.body = NULL; ret = ESP_OK;
out:
    free(pass); free(auth); free(ctx.body);
    return ret;
}
