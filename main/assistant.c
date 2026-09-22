#include "assistant.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"

#include "wifi_manager.h"
#include "wifi_form.h"

static const char *TAG = "assistant";

#define NVS_NS       "app_cfg"
#define K_ONBOARDED  "onbd"
#define K_HUB_EN     "hub_en"
#define K_HUB_TYPE   "hub_type"
#define K_HUB_ADDR   "hub_addr"
#define K_HUB_USER   "hub_user"
#define K_HUB_PASS   "hub_pass"
#define K_BOT_PRIN   "bot_prin"

/* Bots available on the (mock) account. More than one so the UI exercises the
 * "choose a principal bot" flow. */
static const struct {
    const char *id;
    const char *name;
    bool controls_devices;
} BOTS[] = {
    { "grok-1", "Grok \xC2\xB7 Assistant", false },
    { "grok-2", "Grok \xC2\xB7 Casa",      true  },
};
#define BOT_COUNT (sizeof(BOTS) / sizeof(BOTS[0]))

/* ---- NVS helpers ---- */

static uint8_t get_u8(const char *key, uint8_t def)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return def;
    uint8_t v = def;
    nvs_get_u8(h, key, &v);
    nvs_close(h);
    return v;
}

static void get_str(const char *key, char *out, size_t out_len)
{
    out[0] = '\0';
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = out_len;
    nvs_get_str(h, key, out, &len);
    nvs_close(h);
}

/* ---- public API ---- */

bool assistant_onboarding_done(void)
{
    return get_u8(K_ONBOARDED, 0) != 0;
}

esp_err_t assistant_set_hub(bool enabled, const char *type, const char *addr,
                            const char *user, const char *pass)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    nvs_set_u8(h, K_HUB_EN, enabled ? 1 : 0);
    nvs_set_str(h, K_HUB_TYPE, type ? type : "");
    nvs_set_str(h, K_HUB_ADDR, addr ? addr : "");
    nvs_set_str(h, K_HUB_USER, user ? user : "");
    nvs_set_str(h, K_HUB_PASS, pass ? pass : "");
    nvs_set_u8(h, K_ONBOARDED, 1);
    err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "hub %s (type='%s' addr='%s')", enabled ? "configured" : "skipped",
             type ? type : "", addr ? addr : "");
    return err;
}

esp_err_t assistant_set_principal_bot(const char *bot_id)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_set_str(h, K_BOT_PRIN, bot_id ? bot_id : "");
    err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "principal bot set to '%s'", bot_id ? bot_id : "");
    return err;
}

static const char *bot_name(const char *id)
{
    for (size_t i = 0; i < BOT_COUNT; i++) {
        if (strcmp(BOTS[i].id, id) == 0) return BOTS[i].name;
    }
    return BOTS[0].name;
}

void assistant_bots_json(char *buf, size_t buf_len)
{
    strlcpy(buf, "[", buf_len);
    for (size_t i = 0; i < BOT_COUNT; i++) {
        char entry[128];
        snprintf(entry, sizeof(entry),
                 "%s{\"id\":\"%s\",\"name\":\"%s\",\"controlsDevices\":%s}",
                 (i == 0) ? "" : ",", BOTS[i].id, BOTS[i].name,
                 BOTS[i].controls_devices ? "true" : "false");
        strlcat(buf, entry, buf_len);
    }
    strlcat(buf, "]", buf_len);
}

void assistant_devices_json(char *buf, size_t buf_len)
{
    /* Sample devices "controlled by the bot" (mock). */
    strlcpy(buf,
        "["
        "{\"name\":\"Luz da sala\",\"room\":\"Sala\",\"type\":\"light\",\"on\":true,\"detail\":\"80%\"},"
        "{\"name\":\"Luz da cozinha\",\"room\":\"Cozinha\",\"type\":\"light\",\"on\":false,\"detail\":\"\"},"
        "{\"name\":\"Ar-condicionado\",\"room\":\"Quarto\",\"type\":\"ac\",\"on\":true,\"detail\":\"22\xC2\xB0""C\"},"
        "{\"name\":\"Fechadura\",\"room\":\"Entrada\",\"type\":\"lock\",\"on\":false,\"detail\":\"trancada\"},"
        "{\"name\":\"TV\",\"room\":\"Sala\",\"type\":\"tv\",\"on\":false,\"detail\":\"\"},"
        "{\"name\":\"Termostato\",\"room\":\"Sala\",\"type\":\"thermostat\",\"on\":true,\"detail\":\"23\xC2\xB0""C\"}"
        "]", buf_len);
}

void assistant_discover_json(char *buf, size_t buf_len)
{
    /* Mock mDNS-style discovery results. */
    strlcpy(buf,
        "["
        "{\"name\":\"Home Assistant (sala)\",\"addr\":\"homeassistant.local:8123\"},"
        "{\"name\":\"Hub da casa\",\"addr\":\"192.168.0.10:8123\"}"
        "]", buf_len);
}

void assistant_state_json(char *buf, size_t buf_len)
{
    char ssid_esc[96];
    char hub_type[ASSISTANT_STR_MAX] = {0};
    char hub_addr[ASSISTANT_STR_MAX] = {0};
    char principal[ASSISTANT_STR_MAX] = {0};

    wifi_form_json_escape(wifi_manager_get_ssid(), ssid_esc, sizeof(ssid_esc));
    get_str(K_HUB_TYPE, hub_type, sizeof(hub_type));
    get_str(K_HUB_ADDR, hub_addr, sizeof(hub_addr));
    get_str(K_BOT_PRIN, principal, sizeof(principal));

    bool hub_enabled = get_u8(K_HUB_EN, 0) != 0;
    bool hub_configured = hub_enabled && hub_addr[0] != '\0';

    snprintf(buf, buf_len,
        "{\"connected\":%s,\"ssid\":\"%s\",\"ip\":\"%s\","
        "\"onboardingDone\":%s,\"hubEnabled\":%s,\"hubType\":\"%s\",\"hubAddr\":\"%s\","
        "\"hubConfigured\":%s,\"principalBot\":\"%s\",\"botCount\":%u,"
        "\"backend\":\"%s\"}",
        wifi_manager_is_connected() ? "true" : "false",
        ssid_esc,
        wifi_manager_get_ip(),
        assistant_onboarding_done() ? "true" : "false",
        hub_enabled ? "true" : "false",
        hub_type,
        hub_addr,
        hub_configured ? "true" : "false",
        principal,
        (unsigned)BOT_COUNT,
#if CONFIG_APP_BOT_BACKEND_MOCK
        "mock"
#else
        "live"
#endif
    );
}

void assistant_chat_reply(const char *message, char *out, size_t out_len)
{
    char principal[ASSISTANT_STR_MAX] = {0};
    get_str(K_BOT_PRIN, principal, sizeof(principal));
    const char *name = bot_name(principal[0] ? principal : BOTS[0].id);

    char msg[160];
    strlcpy(msg, message ? message : "", sizeof(msg));

#if CONFIG_APP_BOT_BACKEND_MOCK
    bool hub_enabled = get_u8(K_HUB_EN, 0) != 0;
    snprintf(out, out_len,
             "%s (resposta simulada): voce disse \"%s\". %s",
             name, msg,
             hub_enabled ? "Posso acionar os dispositivos conectados."
                         : "Conecte um hub para eu controlar seus dispositivos.");
#else
    (void)name;
    snprintf(out, out_len,
             "Backend real do Grok ainda nao configurado. Ative as credenciais para conversar.");
#endif
}
