#pragma once
#include <stddef.h>
#include <stdint.h>

/* User settings for the CYD screen, persisted in NVS (namespace app_cfg). */
typedef struct {
    char ents[256];      /* comma-separated HA entities shown as rows */
    uint8_t pets;        /* show today's pet food portions row */
    char cam_bell[64];   /* camera shown on doorbell */
    char cam_quick[64];  /* camera of the quick button */
    uint8_t bell_secs;   /* doorbell viewer duration (s) */
    uint16_t cam_ms;     /* snapshot refresh (ms) */
    uint8_t inv;         /* panel colour inversion */
    uint8_t bgr;         /* BGR element order (reboot) */
    uint8_t bright;      /* backlight 5..100 % */
    uint16_t saver_s;    /* idle seconds before the rest screen */
    uint8_t cat;         /* cat animation */
    uint8_t clk24;       /* 24 h clock */
    uint8_t alerts;      /* show alerts banner/badge */
    char net_url[128];   /* stored in NVS; the globe probe does not read it */
    char btn_label[12];  /* quick button label */
    uint8_t btn_icon;    /* 0 gate (portao), 1 camera */
} cyd_cfg_t;

void cyd_cfg_load(void);
const cyd_cfg_t *cyd_cfg(void);
/* Apply fields from a urlencoded form (only keys present), save to NVS. */
void cyd_cfg_set_form(const char *body);
void cyd_cfg_set_u8(const char *key, uint8_t v);
void cyd_cfg_json(char *buf, size_t len);
