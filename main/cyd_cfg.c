#include "cyd_cfg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nvs.h"
#include "wifi_form.h"

static cyd_cfg_t s_cfg;
static int s_loaded;

#define DEF_ENTS "switch.garagem_switch_1=Garagem 1,switch.garagem_switch_2=Garagem 2,switch.piscina,switch.poco"
#define DEF_NET  "http://connectivitycheck.gstatic.com/generate_204"

static void gstr(nvs_handle_t h, const char *k, char *out, size_t n, const char *def)
{
    size_t len = n;
    if (!h || nvs_get_str(h, k, out, &len) != ESP_OK) snprintf(out, n, "%s", def);
}
static uint8_t gu8(nvs_handle_t h, const char *k, uint8_t def)
{
    uint8_t v = def;
    if (h) nvs_get_u8(h, k, &v);
    return v;
}
static uint16_t gu16(nvs_handle_t h, const char *k, uint16_t def)
{
    uint16_t v = def;
    if (h) nvs_get_u16(h, k, &v);
    return v;
}

void cyd_cfg_load(void)
{
    nvs_handle_t h = 0;
    if (nvs_open("app_cfg", NVS_READONLY, &h) != ESP_OK) h = 0;
    gstr(h, "c_ents", s_cfg.ents, sizeof s_cfg.ents, DEF_ENTS);
    s_cfg.pets = gu8(h, "c_pets", 1);
    gstr(h, "c_cbell", s_cfg.cam_bell, sizeof s_cfg.cam_bell, "camera.frigate_frente_esquerda");
    gstr(h, "c_cquick", s_cfg.cam_quick, sizeof s_cfg.cam_quick, "camera.frigate_frente_direita");
    s_cfg.bell_secs = gu8(h, "c_bsecs", 20);
    s_cfg.cam_ms = gu16(h, "c_camms", 1000);
    s_cfg.inv = gu8(h, "disp_inv", 0);   /* v4: Rodrigo's panel needs no inversion */
    s_cfg.bgr = gu8(h, "disp_bgr", 1);
    s_cfg.bright = gu8(h, "c_bright", 100);
    s_cfg.saver_s = gu16(h, "c_saver", 30);
    s_cfg.cat = gu8(h, "c_cat", 1);
    s_cfg.clk24 = gu8(h, "c_clk24", 1);
    s_cfg.alerts = gu8(h, "c_alerts", 1);
    gstr(h, "c_blabel", s_cfg.btn_label, sizeof s_cfg.btn_label, "Porta");
    s_cfg.btn_icon = gu8(h, "c_bicon", 0);
    gstr(h, "c_net", s_cfg.net_url, sizeof s_cfg.net_url, DEF_NET);
    if (h) nvs_close(h);
    s_loaded = 1;
}

const cyd_cfg_t *cyd_cfg(void)
{
    if (!s_loaded) cyd_cfg_load();
    return &s_cfg;
}

void cyd_cfg_set_u8(const char *key, uint8_t v)
{
    nvs_handle_t h;
    if (nvs_open("app_cfg", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, key, v);
        nvs_commit(h);
        nvs_close(h);
    }
    cyd_cfg_load();
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void cyd_cfg_set_form(const char *body)
{
    nvs_handle_t h;
    if (nvs_open("app_cfg", NVS_READWRITE, &h) != ESP_OK) return;
    char v[256];
    struct { const char *f, *k; } strs[] = {
        {"ents", "c_ents"}, {"cam_bell", "c_cbell"}, {"cam_quick", "c_cquick"}, {"net_url", "c_net"}, {"btn_label", "c_blabel"},
    };
    for (size_t i = 0; i < sizeof strs / sizeof strs[0]; i++) {
        if (wifi_form_get_field(body, strs[i].f, v, sizeof v)) nvs_set_str(h, strs[i].k, v);
    }
    struct { const char *f, *k; int lo, hi; } u8s[] = {
        {"pets", "c_pets", 0, 1}, {"bell_secs", "c_bsecs", 3, 120}, {"inv", "disp_inv", 0, 1},
        {"bgr", "disp_bgr", 0, 1}, {"bright", "c_bright", 5, 100}, {"cat", "c_cat", 0, 1},
        {"clk24", "c_clk24", 0, 1}, {"alerts", "c_alerts", 0, 1}, {"btn_icon", "c_bicon", 0, 1},
    };
    for (size_t i = 0; i < sizeof u8s / sizeof u8s[0]; i++) {
        if (wifi_form_get_field(body, u8s[i].f, v, sizeof v) && v[0])
            nvs_set_u8(h, u8s[i].k, (uint8_t)clampi(atoi(v), u8s[i].lo, u8s[i].hi));
    }
    if (wifi_form_get_field(body, "cam_ms", v, sizeof v) && v[0]) nvs_set_u16(h, "c_camms", (uint16_t)clampi(atoi(v), 300, 10000));
    if (wifi_form_get_field(body, "saver_s", v, sizeof v) && v[0]) nvs_set_u16(h, "c_saver", (uint16_t)clampi(atoi(v), 10, 3600));
    nvs_commit(h);
    nvs_close(h);
    cyd_cfg_load();
}

void cyd_cfg_json(char *buf, size_t len)
{
    const cyd_cfg_t *c = cyd_cfg();
    char e[300], b[100], q[100], n[180], l[30];
    wifi_form_json_escape(c->btn_label, l, sizeof l);
    wifi_form_json_escape(c->ents, e, sizeof e);
    wifi_form_json_escape(c->cam_bell, b, sizeof b);
    wifi_form_json_escape(c->cam_quick, q, sizeof q);
    wifi_form_json_escape(c->net_url, n, sizeof n);
    snprintf(buf, len,
             "{\"ents\":\"%s\",\"pets\":%u,\"cam_bell\":\"%s\",\"cam_quick\":\"%s\",\"bell_secs\":%u,"
             "\"cam_ms\":%u,\"inv\":%u,\"bgr\":%u,\"bright\":%u,\"saver_s\":%u,\"cat\":%u,\"clk24\":%u,"
             "\"alerts\":%u,\"net_url\":\"%s\",\"btn_label\":\"%s\",\"btn_icon\":%u}",
             e, c->pets, b, q, c->bell_secs, c->cam_ms, c->inv, c->bgr, c->bright, c->saver_s,
             c->cat, c->clk24, c->alerts, n, l, c->btn_icon);
}
