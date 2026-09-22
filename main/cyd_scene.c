#include "cyd_scene.h"

#include <string.h>

#include "cyd_font.inc"

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static const uint8_t SEG[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

typedef struct {
    uint16_t *band;
    int y0;
    int bh;
    uint16_t bg;
    uint16_t cream;
    uint16_t orange;
    uint16_t black;
    uint16_t pink;
    uint16_t white;
    uint16_t warm;
    uint16_t cool;
    uint16_t muted;
} canvas_t;

static void clear_band(canvas_t *c)
{
    for (int i = 0; i < c->bh * CYD_W; i++) {
        c->band[i] = c->bg;
    }
}

static void fill_rect(canvas_t *c, int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    int x1 = x < 0 ? 0 : x;
    int y1 = y < c->y0 ? c->y0 : y;
    int x2 = x + w;
    int y2 = y + h;
    if (x2 > CYD_W) {
        x2 = CYD_W;
    }
    if (y2 > c->y0 + c->bh) {
        y2 = c->y0 + c->bh;
    }
    if (y2 > CYD_H) {
        y2 = CYD_H;
    }
    for (int yy = y1; yy < y2; yy++) {
        uint16_t *row = c->band + (yy - c->y0) * CYD_W;
        for (int xx = x1; xx < x2; xx++) {
            row[xx] = color;
        }
    }
}

static void fill_disc(canvas_t *c, int cx, int cy, int r, uint16_t color)
{
    if (r <= 0) {
        return;
    }
    int y1 = cy - r;
    int y2 = cy + r;
    if (y1 < c->y0) {
        y1 = c->y0;
    }
    if (y2 >= c->y0 + c->bh) {
        y2 = c->y0 + c->bh - 1;
    }
    if (y2 >= CYD_H) {
        y2 = CYD_H - 1;
    }
    int r2 = r * r;
    for (int y = y1; y <= y2; y++) {
        int dy = y - cy;
        int rest = r2 - dy * dy;
        if (rest < 0) {
            continue;
        }
        int dx = 0;
        while ((dx + 1) * (dx + 1) <= rest) {
            dx++;
        }
        int x1 = cx - dx;
        int x2 = cx + dx;
        if (x1 < 0) {
            x1 = 0;
        }
        if (x2 >= CYD_W) {
            x2 = CYD_W - 1;
        }
        uint16_t *row = c->band + (y - c->y0) * CYD_W;
        for (int x = x1; x <= x2; x++) {
            row[x] = color;
        }
    }
}

static void fill_tri(canvas_t *c, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color)
{
    int ys[3] = {y0, y1, y2};
    int xs[3] = {x0, x1, x2};
    for (int y = c->y0; y < c->y0 + c->bh && y < CYD_H; y++) {
        int hits[3];
        int n = 0;
        for (int e = 0; e < 3; e++) {
            int ya = ys[e];
            int yb = ys[(e + 1) % 3];
            int xa = xs[e];
            int xb = xs[(e + 1) % 3];
            int ymin = ya < yb ? ya : yb;
            int ymax = ya > yb ? ya : yb;
            if (y < ymin || y >= ymax || ya == yb) {
                continue;
            }
            hits[n++] = xa + (xb - xa) * (y - ya) / (yb - ya);
        }
        if (n < 2) {
            continue;
        }
        int left = hits[0];
        int right = hits[1];
        if (n == 3) {
            if (hits[2] < left) {
                left = hits[2];
            }
            if (hits[2] > right) {
                right = hits[2];
            }
        }
        if (left > right) {
            int tmp = left;
            left = right;
            right = tmp;
        }
        if (left < 0) {
            left = 0;
        }
        if (right >= CYD_W) {
            right = CYD_W - 1;
        }
        uint16_t *row = c->band + (y - c->y0) * CYD_W;
        for (int x = left; x <= right; x++) {
            row[x] = color;
        }
    }
}

static void glyph(canvas_t *c, int x, int y, int scale, char ch, uint16_t color)
{
    unsigned uc = (unsigned char)ch;
    if (uc >= 128) {
        uc = '?';
    }
    const uint8_t *rows = FONT8[uc];
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (rows[row] & (1 << col)) {
                fill_rect(c, x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

static int text_px(const char *s, int scale)
{
    return (int)strlen(s) * 8 * scale;
}

static void text(canvas_t *c, int x, int y, int scale, const char *s, uint16_t color)
{
    for (int i = 0; s[i]; i++) {
        glyph(c, x + i * 8 * scale, y, scale, s[i], color);
    }
}

static void text_center(canvas_t *c, int y, int scale, const char *s, uint16_t color)
{
    text(c, (CYD_W - text_px(s, scale)) / 2, y, scale, s, color);
}

static void seg_h(canvas_t *c, int x, int y, int w, int t, uint16_t color)
{
    fill_rect(c, x + t / 2, y, w - t, t, color);
}

static void seg_v(canvas_t *c, int x, int y, int h, int t, uint16_t color)
{
    fill_rect(c, x, y + t / 2, t, h - t, color);
}

static void digit(canvas_t *c, int x, int y, int w, int h, int t, int mask, uint16_t color)
{
    int half = h / 2;
    if (mask & 0x01) {
        seg_h(c, x, y, w, t, color);
    }
    if (mask & 0x02) {
        seg_v(c, x + w - t, y, half, t, color);
    }
    if (mask & 0x04) {
        seg_v(c, x + w - t, y + half, half, t, color);
    }
    if (mask & 0x08) {
        seg_h(c, x, y + h - t, w, t, color);
    }
    if (mask & 0x10) {
        seg_v(c, x, y + half, half, t, color);
    }
    if (mask & 0x20) {
        seg_v(c, x, y, half, t, color);
    }
    if (mask & 0x40) {
        seg_h(c, x, y + half - t / 2, w, t, color);
    }
}

static int clock_char(canvas_t *c, int x, int y, int w, int h, int t, char ch, uint16_t color)
{
    if (ch >= '0' && ch <= '9') {
        digit(c, x, y, w, h, t, SEG[ch - '0'], color);
        return w;
    }
    if (ch == '-') {
        digit(c, x, y, w, h, t, 0x40, color);
        return w;
    }
    if (ch == ':') {
        int s = t;
        if (s < 4) {
            s = 4;
        }
        fill_rect(c, x + 2, y + h / 3 - s / 2, s, s, color);
        fill_rect(c, x + 2, y + (2 * h) / 3 - s / 2, s, s, color);
        return s + 6;
    }
    return w;
}

static void clock_row(canvas_t *c, int y, int w, int h, int t, int gap, const char *hhmm, uint16_t color)
{
    int total = 0;
    for (int i = 0; hhmm[i]; i++) {
        total += (hhmm[i] == ':') ? (t < 4 ? 10 : t + 6) : w;
        if (hhmm[i + 1]) {
            total += gap;
        }
    }
    int x = (CYD_W - total) / 2;
    for (int i = 0; hhmm[i]; i++) {
        x += clock_char(c, x, y, w, h, t, hhmm[i], color);
        x += gap;
    }
}

static void kitten(canvas_t *c, int ox, int oy, int blink, int tail)
{
    int sway = (tail % 3) - 1;
    fill_disc(c, ox + 16 + sway * 4, oy + 18, 7, c->black);
    fill_disc(c, ox + 22 + sway * 3, oy + 30, 7, c->orange);
    fill_disc(c, ox + 28 + sway * 2, oy + 40, 8, c->cream);
    fill_disc(c, ox + 50, oy + 52, 20, c->cream);
    fill_disc(c, ox + 38, oy + 50, 9, c->orange);
    fill_disc(c, ox + 62, oy + 46, 8, c->black);
    fill_disc(c, ox + 40, oy + 68, 7, c->cream);
    fill_disc(c, ox + 60, oy + 68, 7, c->cream);
    fill_tri(c, ox + 30, oy + 28, ox + 18, oy + 4, ox + 46, oy + 22, c->black);
    fill_tri(c, ox + 70, oy + 28, ox + 84, oy + 2, ox + 54, oy + 22, c->orange);
    fill_disc(c, ox + 50, oy + 32, 18, c->cream);
    fill_disc(c, ox + 28, oy + 16, 6, c->pink);
    fill_disc(c, ox + 74, oy + 14, 6, c->pink);
    fill_disc(c, ox + 38, oy + 26, 8, c->black);
    fill_disc(c, ox + 64, oy + 40, 7, c->orange);
    if (blink) {
        fill_rect(c, ox + 40, oy + 34, 8, 2, c->black);
        fill_rect(c, ox + 54, oy + 34, 8, 2, c->black);
    } else {
        fill_disc(c, ox + 43, oy + 34, 3, c->black);
        fill_disc(c, ox + 57, oy + 34, 3, c->black);
        fill_rect(c, ox + 44, oy + 32, 2, 2, c->white);
        fill_rect(c, ox + 58, oy + 32, 2, 2, c->white);
    }
    fill_disc(c, ox + 50, oy + 40, 2, c->pink);
}

static void paint_portal(canvas_t *c)
{
    text_center(c, 28, 2, "Conecte no Wi-Fi", c->white);
    text_center(c, 64, 3, "ESP32-Setup", c->warm);
    text_center(c, 108, 2, "senha", c->muted);
    text_center(c, 136, 3, "esp32setup", c->white);
    text_center(c, 184, 2, "Abra no celular", c->muted);
    text_center(c, 208, 2, "192.168.4.1", c->cool);
}

static void format_date(char *out, int cap, const char *wd, int day, const char *mo)
{
    /* "seg, 22 set" */
    int n = 0;
    for (int i = 0; wd[i] && n < cap - 1; i++) {
        out[n++] = wd[i];
    }
    if (n < cap - 1) {
        out[n++] = ',';
    }
    if (n < cap - 1) {
        out[n++] = ' ';
    }
    if (day >= 10 && n < cap - 1) {
        out[n++] = (char)('0' + day / 10);
    }
    if (n < cap - 1) {
        out[n++] = (char)('0' + day % 10);
    }
    if (n < cap - 1) {
        out[n++] = ' ';
    }
    for (int i = 0; mo[i] && n < cap - 1; i++) {
        out[n++] = mo[i];
    }
    out[n] = '\0';
}

static void date_line(const cyd_scene_t *scene, char *out, int cap)
{
    static const char *wd[] = {"dom", "seg", "ter", "qua", "qui", "sex", "sab"};
    static const char *mo[] = {"jan", "fev", "mar", "abr", "mai", "jun",
                               "jul", "ago", "set", "out", "nov", "dez"};
    int w = scene->wday;
    int m = scene->month;
    if (w < 0 || w > 6) {
        w = 0;
    }
    if (m < 0 || m > 11) {
        m = 0;
    }
    format_date(out, cap, wd[w], scene->mday, mo[m]);
}

static void weather_text(char *left, char *right, int temp, int humidity)
{
    left[0] = (char)('0' + (temp / 10) % 10);
    left[1] = (char)('0' + (temp < 0 ? 0 : temp % 10));
    left[2] = 'c';
    left[3] = '\0';
    right[0] = (char)('0' + (humidity / 10) % 10);
    right[1] = (char)('0' + humidity % 10);
    right[2] = '%';
    right[3] = '\0';
}

static void weather_pair(canvas_t *c, int x, int y, int scale, int temp, int humidity)
{
    char left[8];
    char right[8];
    weather_text(left, right, temp, humidity);
    int lw = text_px(left, scale);
    text(c, x, y, scale, left, c->warm);
    text(c, x + lw + 16, y, scale, right, c->cool);
}

static void paint_saver(canvas_t *c, const cyd_scene_t *scene)
{
    char hhmm[6];
    if (scene->time_valid) {
        hhmm[0] = (char)('0' + scene->hour / 10);
        hhmm[1] = (char)('0' + scene->hour % 10);
        hhmm[2] = ':';
        hhmm[3] = (char)('0' + scene->minute / 10);
        hhmm[4] = (char)('0' + scene->minute % 10);
        hhmm[5] = '\0';
    } else {
        memcpy(hhmm, "--:--", 6);
    }
    clock_row(c, 8, 46, 78, 8, 6, hhmm, c->white);
    if (scene->time_valid) {
        char date[20];
        date_line(scene, date, sizeof date);
        text_center(c, 96, 2, date, c->muted);
    }
    weather_pair(c, 104, 120, 2, scene->temp_c, scene->humidity);
    kitten(c, 118, 140, 1, scene->tail);
    text_center(c, 224, 1, "toque na tela", c->muted);
}

static void paint_awake(canvas_t *c, const cyd_scene_t *scene)
{
    char hhmm[6];
    if (scene->time_valid) {
        hhmm[0] = (char)('0' + scene->hour / 10);
        hhmm[1] = (char)('0' + scene->hour % 10);
        hhmm[2] = ':';
        hhmm[3] = (char)('0' + scene->minute / 10);
        hhmm[4] = (char)('0' + scene->minute % 10);
        hhmm[5] = '\0';
    } else {
        memcpy(hhmm, "--:--", 6);
    }
    text(c, 8, 8, 2, hhmm, c->white);
    weather_pair(c, 168, 8, 2, scene->temp_c, scene->humidity);
    kitten(c, 112, 36, scene->blink, scene->tail);
    text_center(c, 150, 2, "Gatinho", c->warm);
    if (scene->ip[0]) {
        text_center(c, 178, 2, scene->ip, c->cool);
    }
    text_center(c, 214, 1, "sem toque, volta o relogio", c->muted);
}

void cyd_scene_paint(uint16_t *band, int y0, int band_h, const cyd_scene_t *scene)
{
    canvas_t c = {
        .band = band,
        .y0 = y0,
        .bh = band_h,
        .bg = rgb565(11, 18, 32),
        .cream = rgb565(246, 241, 232),
        .orange = rgb565(239, 138, 54),
        .black = rgb565(42, 36, 31),
        .pink = rgb565(243, 166, 184),
        .white = rgb565(248, 250, 252),
        .warm = rgb565(253, 186, 116),
        .cool = rgb565(125, 211, 252),
        .muted = rgb565(182, 196, 214),
    };
    clear_band(&c);
    if (scene->mode == CYD_UI_PORTAL) {
        paint_portal(&c);
    } else if (scene->mode == CYD_UI_SAVER) {
        paint_saver(&c, scene);
    } else {
        paint_awake(&c, scene);
    }
}
