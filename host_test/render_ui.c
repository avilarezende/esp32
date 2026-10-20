/*
 * Host-side renderer for the CYD pages. Paints with cyd_scene_paint (the
 * firmware painter, one 20 px band at a time) and writes 24-bit BMPs.
 *
 *   host_test/build/render_ui [outdir]
 */

#include <stdio.h>
#include <string.h>

#include "cyd_bmp.h"
#include "cyd_cfg.h"
#include "cyd_scene.h"

static cyd_cfg_t s_cfg = {
    .btn_icon = 0,
    .btn_label = "Porta",
    .cat = 1,
};

const cyd_cfg_t *cyd_cfg(void)
{
    return &s_cfg;
}

static uint16_t frame[CYD_W * CYD_H];
static uint16_t band[CYD_W * 20];

static void paint(const cyd_scene_t *scene)
{
    for (int y = 0; y < CYD_H; y += 20) {
        cyd_scene_paint(band, y, 20, scene);
        memcpy(frame + y * CYD_W, band, (size_t)CYD_W * 20 * sizeof(uint16_t));
    }
}

static int write_bmp(const char *path)
{
    FILE *f = fopen(path, "wb");
    uint8_t hdr[CYD_BMP_HEADER];
    uint8_t row[CYD_BMP_ROW];
    if (!f) {
        perror(path);
        return 1;
    }
    cyd_bmp_header(hdr);
    if (fwrite(hdr, 1, sizeof hdr, f) != sizeof hdr) {
        fclose(f);
        return 1;
    }
    for (int y = CYD_H - 1; y >= 0; y--) {
        cyd_bmp_row(row, frame + y * CYD_W, CYD_W);
        if (fwrite(row, 1, sizeof row, f) != sizeof row) {
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

static void save(const char *dir, const char *name, const cyd_scene_t *scene)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.bmp", dir, name);
    paint(scene);
    if (write_bmp(path) != 0) {
        fprintf(stderr, "failed %s\n", path);
    } else {
        printf("wrote %s\n", path);
    }
}

static void base(cyd_scene_t *s)
{
    memset(s, 0, sizeof *s);
    s->time_valid = 1;
    s->hour = 9;
    s->minute = 35;
    s->wday = 2;
    s->mday = 20;
    s->month = 9;
    s->temp_c = 29;
    s->humidity = 64;
    s->has_temp = 1;
    s->has_humidity = 1;
    s->cat_on = 1;
    s->behavior = 0;
    s->phase = 4;
    s->tail = 2;
    s->net_state = 2;
    s->net_frame = 4;
    s->cam_btn = 1;
    snprintf(s->place, sizeof s->place, "Casa");
    snprintf(s->status, sizeof s->status, "HA ok");
}

static void add_rows(cyd_scene_t *s)
{
    s->row_count = 4;
    snprintf(s->rows[0].name, sizeof s->rows[0].name, "Garagem 1");
    snprintf(s->rows[0].detail, sizeof s->rows[0].detail, "ligado");
    s->rows[0].on = 1;
    snprintf(s->rows[1].name, sizeof s->rows[1].name, "Garagem 2");
    snprintf(s->rows[1].detail, sizeof s->rows[1].detail, "desligado");
    s->rows[1].on = 0;
    snprintf(s->rows[2].name, sizeof s->rows[2].name, "Piscina");
    snprintf(s->rows[2].detail, sizeof s->rows[2].detail, "ligado");
    s->rows[2].on = 1;
    snprintf(s->rows[3].name, sizeof s->rows[3].name, "Poco");
    snprintf(s->rows[3].detail, sizeof s->rows[3].detail, "12.4");
    s->rows[3].on = -1;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "host_test/build/preview";
    cyd_scene_t s;

    base(&s);
    s.mode = CYD_UI_SAVER;
    save(dir, "saver", &s);

    s.alert_count = 1;
    s.alert_rows = 1;
    s.banner = 1;
    s.alerts[0].level = 2;
    snprintf(s.alerts[0].title, sizeof s.alerts[0].title, "Portao aberto");
    snprintf(s.alerts[0].msg, sizeof s.alerts[0].msg, "frente direita");
    save(dir, "saver-alert", &s);

    base(&s);
    s.mode = CYD_UI_HOME;
    add_rows(&s);
    s.net_state = 2;
    s.net_frame = 4;
    save(dir, "home", &s);
    save(dir, "status-on", &s);
    for (int f = 0; f < 8; f++) {
        char name[16];
        s.net_frame = f;
        snprintf(name, sizeof name, "spin-%d", f);
        save(dir, name, &s);
    }
    s.net_frame = 4;

    s.net_state = 0;
    s.net_frame = 4;
    save(dir, "status-off", &s);
    save(dir, "home-net-off", &s);
    s.net_state = 2;
    s.net_frame = 4;

    s.alert_count = 2;
    s.alert_rows = 1;
    s.banner = 1;
    s.alerts[0].level = 1;
    snprintf(s.alerts[0].title, sizeof s.alerts[0].title, "Campainha");
    snprintf(s.alerts[0].msg, sizeof s.alerts[0].msg, "frente esquerda");
    save(dir, "home-alert", &s);

    base(&s);
    s.mode = CYD_UI_HOME;
    s.has_temp = 0;
    s.has_humidity = 0;
    s.net_state = 1;
    snprintf(s.status, sizeof s.status, "sem hub");
    save(dir, "home-empty", &s);

    base(&s);
    s.mode = CYD_UI_HOME;
    s.row_count = 0;
    snprintf(s.status, sizeof s.status, "offline");
    save(dir, "home-offline", &s);

    s_cfg.btn_icon = 1;
    snprintf(s_cfg.btn_label, sizeof s_cfg.btn_label, "Frente");
    base(&s);
    s.mode = CYD_UI_HOME;
    add_rows(&s);
    save(dir, "home-camera", &s);
    s_cfg.btn_icon = 0;
    snprintf(s_cfg.btn_label, sizeof s_cfg.btn_label, "Porta");

    base(&s);
    s.mode = CYD_UI_ALERTS;
    s.updates = 2;
    s.alert_count = 3;
    s.alert_rows = 3;
    s.alerts[0].level = 2;
    snprintf(s.alerts[0].title, sizeof s.alerts[0].title, "Portao aberto");
    snprintf(s.alerts[0].msg, sizeof s.alerts[0].msg, "frente direita");
    s.alerts[1].level = 1;
    snprintf(s.alerts[1].title, sizeof s.alerts[1].title, "Campainha");
    snprintf(s.alerts[1].msg, sizeof s.alerts[1].msg, "ha 2 min");
    s.alerts[2].level = 0;
    snprintf(s.alerts[2].title, sizeof s.alerts[2].title, "Atualizacao");
    snprintf(s.alerts[2].msg, sizeof s.alerts[2].msg, "Home Assistant");
    save(dir, "alerts", &s);

    base(&s);
    s.mode = CYD_UI_ALERTS;
    s.net_state = 3;
    save(dir, "alerts-empty", &s);

    base(&s);
    s.mode = CYD_UI_PORTAL;
    s.net_state = 0;
    save(dir, "portal", &s);

    /* Cat poses on the saver, plus a short strip for the animated preview. */
    base(&s);
    s.mode = CYD_UI_SAVER;
    s.behavior = CYD_CAT_IDLE;
    s.phase = 4;
    save(dir, "cat-idle", &s);
    s.phase = 25;
    save(dir, "cat-blink", &s);
    s.behavior = CYD_CAT_STRETCH;
    s.phase = 8;
    save(dir, "cat-stretch", &s);
    s.behavior = CYD_CAT_SLEEP;
    s.phase = 4;
    s.hour = 23;
    save(dir, "cat-sleep", &s);
    s.behavior = CYD_CAT_ALERT;
    s.phase = 0;
    s.hour = 9;
    save(dir, "cat-alert", &s);
    s.behavior = CYD_CAT_PURR;
    s.phase = 2;
    save(dir, "cat-purr", &s);

    {
        static const int seq[][2] = {
            {CYD_CAT_IDLE, 0}, {CYD_CAT_IDLE, 2}, {CYD_CAT_IDLE, 4},
            {CYD_CAT_IDLE, 6}, {CYD_CAT_IDLE, 8}, {CYD_CAT_IDLE, 10},
            {CYD_CAT_IDLE, 24}, {CYD_CAT_IDLE, 25}, {CYD_CAT_IDLE, 26},
            {CYD_CAT_IDLE, 12}, {CYD_CAT_STRETCH, 2}, {CYD_CAT_STRETCH, 4},
            {CYD_CAT_STRETCH, 6}, {CYD_CAT_STRETCH, 8}, {CYD_CAT_STRETCH, 12},
            {CYD_CAT_SLEEP, 0}, {CYD_CAT_SLEEP, 4}, {CYD_CAT_SLEEP, 8},
            {CYD_CAT_ALERT, 0}, {CYD_CAT_ALERT, 6},
            {CYD_CAT_PURR, 0}, {CYD_CAT_PURR, 2}, {CYD_CAT_PURR, 4},
        };
        int i;
        for (i = 0; i < (int)(sizeof seq / sizeof seq[0]); i++) {
            char name[32];
            base(&s);
            s.mode = CYD_UI_SAVER;
            s.behavior = seq[i][0];
            s.phase = seq[i][1];
            if (s.behavior == CYD_CAT_SLEEP) {
                s.hour = 23;
            }
            snprintf(name, sizeof name, "cat-anim-%02d", i);
            save(dir, name, &s);
        }
    }

    return 0;
}
