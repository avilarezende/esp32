#include "cyd_scene.h"

#include <string.h>

#include "cyd_font.inc"

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

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

static void fill_ellipse(canvas_t *c, int cx, int cy, int rx, int ry, uint16_t color)
{
    if (rx < 1 || ry < 1) {
        return;
    }
    int y1 = cy - ry;
    int y2 = cy + ry;
    if (y1 < c->y0) {
        y1 = c->y0;
    }
    if (y2 >= c->y0 + c->bh) {
        y2 = c->y0 + c->bh - 1;
    }
    if (y2 >= CYD_H) {
        y2 = CYD_H - 1;
    }
    int rx2 = rx * rx;
    int ry2 = ry * ry;
    for (int y = y1; y <= y2; y++) {
        int dy = y - cy;
        int span = rx2 * (ry2 - dy * dy);
        if (span < 0) {
            continue;
        }
        int dx = 0;
        while ((dx + 1) * (dx + 1) * ry2 <= span) {
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

static void plot(canvas_t *c, int x, int y, uint16_t color)
{
    fill_rect(c, x, y, 1, 1, color);
}

static void line(canvas_t *c, int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    if (dx < 0) {
        dx = -dx;
    }
    if (dy < 0) {
        dy = -dy;
    }
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        plot(c, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = err * 2;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
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

/* Geometric sans for the clock, in a 24x40 box. Round counters, not segments. */
static int cu(int v, int h)
{
    return v * h / 40;
}

static int cstroke(int h)
{
    int r = (h + 8) / 12;
    return r < 1 ? 1 : r;
}

static void stamp(canvas_t *c, int x0, int y0, int x1, int y1, int r, uint16_t color)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    int n = adx > ady ? adx : ady;
    if (n < 1) {
        n = 1;
    }
    if (n > 28) {
        n = 28;
    }
    for (int i = 0; i <= n; i++) {
        fill_disc(c, x0 + dx * i / n, y0 + dy * i / n, r, color);
    }
}

static void dline(canvas_t *c, int ox, int oy, int h, int x0, int y0, int x1, int y1, uint16_t color)
{
    stamp(c, ox + cu(x0, h), oy + cu(y0, h), ox + cu(x1, h), oy + cu(y1, h), cstroke(h), color);
}

static void cubic(int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3,
                  int i, int steps, int *xo, int *yo);

static void dcurve(canvas_t *c, int ox, int oy, int h,
                   int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3,
                   uint16_t color)
{
    int r = cstroke(h);
    for (int i = 0; i <= 8; i++) {
        int x;
        int y;
        cubic(x0, y0, x1, y1, x2, y2, x3, y3, i, 8, &x, &y);
        fill_disc(c, ox + cu(x, h), oy + cu(y, h), r, color);
    }
}

static void dring(canvas_t *c, int ox, int oy, int h, int cx, int cy, int rx, int ry, uint16_t color)
{
    int t = cstroke(h);
    int px = ox + cu(cx, h);
    int py = oy + cu(cy, h);
    int orx = cu(rx, h);
    int ory = cu(ry, h);
    if (orx < t + 1) {
        orx = t + 1;
    }
    if (ory < t + 1) {
        ory = t + 1;
    }
    fill_ellipse(c, px, py, orx, ory, color);
    fill_ellipse(c, px, py, orx - t, ory - t, c->bg);
}

static int clock_char(canvas_t *c, int x, int y, int h, char ch, uint16_t color)
{
    int adv = cu(26, h);
    if (ch == ':') {
        int r = cstroke(h);
        if (r < 2 && h >= 16) {
            r = 2;
        }
        fill_disc(c, x + cu(4, h), y + cu(14, h), r, color);
        fill_disc(c, x + cu(4, h), y + cu(27, h), r, color);
        return cu(10, h);
    }
    if (ch == '-') {
        dline(c, x, y, h, 4, 20, 20, 20, color);
        return adv;
    }
    if (ch < '0' || ch > '9') {
        return adv;
    }
    switch (ch) {
    case '0':
        dring(c, x, y, h, 12, 20, 10, 17, color);
        break;
    case '1':
        dline(c, x, y, h, 8, 11, 13, 5, color);
        dline(c, x, y, h, 13, 5, 13, 35, color);
        dline(c, x, y, h, 7, 35, 19, 35, color);
        break;
    case '2':
        dcurve(c, x, y, h, 5, 14, 4, 3, 21, 3, 19, 16, color);
        dline(c, x, y, h, 19, 16, 5, 33, color);
        dline(c, x, y, h, 5, 33, 20, 33, color);
        break;
    case '3':
        dcurve(c, x, y, h, 5, 12, 5, 3, 21, 3, 16, 15, color);
        dcurve(c, x, y, h, 16, 15, 22, 18, 22, 37, 6, 33, color);
        break;
    case '4':
        dline(c, x, y, h, 16, 5, 16, 35, color);
        dline(c, x, y, h, 16, 6, 5, 24, color);
        dline(c, x, y, h, 4, 24, 21, 24, color);
        break;
    case '5':
        dline(c, x, y, h, 18, 6, 6, 6, color);
        dline(c, x, y, h, 6, 6, 7, 17, color);
        dcurve(c, x, y, h, 7, 17, 6, 15, 21, 16, 18, 27, color);
        dcurve(c, x, y, h, 18, 27, 16, 36, 6, 36, 6, 30, color);
        break;
    case '6':
        dcurve(c, x, y, h, 16, 8, 6, 4, 5, 16, 8, 22, color);
        dring(c, x, y, h, 12, 26, 9, 11, color);
        break;
    case '7':
        dline(c, x, y, h, 5, 7, 19, 7, color);
        dline(c, x, y, h, 19, 7, 8, 35, color);
        break;
    case '8':
        dring(c, x, y, h, 12, 13, 8, 9, color);
        dring(c, x, y, h, 12, 28, 9, 10, color);
        break;
    default: /* 9 */
        dring(c, x, y, h, 12, 15, 9, 11, color);
        dcurve(c, x, y, h, 16, 20, 20, 26, 18, 38, 8, 34, color);
        break;
    }
    return adv;
}

static int clock_width(int h, const char *hhmm)
{
    int gap = cu(3, h);
    int total = 0;
    for (int i = 0; hhmm[i]; i++) {
        total += (hhmm[i] == ':') ? cu(10, h) : cu(26, h);
        if (hhmm[i + 1]) {
            total += gap;
        }
    }
    return total;
}

static void clock_at(canvas_t *c, int x, int y, int h, const char *hhmm, uint16_t color)
{
    int gap = cu(3, h);
    for (int i = 0; hhmm[i]; i++) {
        x += clock_char(c, x, y, h, hhmm[i], color);
        if (hhmm[i + 1]) {
            x += gap;
        }
    }
}

static void clock_row(canvas_t *c, int y, int h, const char *hhmm, uint16_t color)
{
    int x = (CYD_W - clock_width(h, hhmm)) / 2;
    clock_at(c, x, y, h, hhmm, color);
}

/* Map a point from the web kitten's 160x140 viewBox. */
static int sc(int v, int n, int d)
{
    return v * n / d;
}

static void cubic(int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3,
                  int i, int steps, int *xo, int *yo)
{
    int t = (i * 32) / steps;
    int u = 32 - t;
    int u2 = u * u;
    int t2 = t * t;
    int den = 32 * 32 * 32;
    *xo = (u2 * u * x0 + 3 * u2 * t * x1 + 3 * u * t2 * x2 + t2 * t * x3) / den;
    *yo = (u2 * u * y0 + 3 * u2 * t * y1 + 3 * u * t2 * y2 + t2 * t * y3) / den;
}

static void stroke_cubic(canvas_t *c, int ox, int oy, int n, int d,
                         int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3,
                         int radius, uint16_t color)
{
    int steps = 10;
    int r = sc(radius, n, d);
    if (r < 1) {
        r = 1;
    }
    for (int i = 0; i <= steps; i++) {
        int x;
        int y;
        cubic(x0, y0, x1, y1, x2, y2, x3, y3, i, steps, &x, &y);
        fill_disc(c, ox + sc(x, n, d), oy + sc(y, n, d), r, color);
    }
}

static void ell(canvas_t *c, int ox, int oy, int n, int d,
                int cx, int cy, int rx, int ry, uint16_t color)
{
    int erx = sc(rx, n, d);
    int ery = sc(ry, n, d);
    if (erx < 1) {
        erx = 1;
    }
    if (ery < 1) {
        ery = 1;
    }
    fill_ellipse(c, ox + sc(cx, n, d), oy + sc(cy, n, d), erx, ery, color);
}

static void tri(canvas_t *c, int ox, int oy, int n, int d,
                int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color)
{
    fill_tri(c,
             ox + sc(x0, n, d), oy + sc(y0, n, d),
             ox + sc(x1, n, d), oy + sc(y1, n, d),
             ox + sc(x2, n, d), oy + sc(y2, n, d),
             color);
}

static void kitten(canvas_t *c, int ox, int oy, int n, int d, int blink, int tail)
{
    /* Same shapes as the web SVG (viewBox 160x140): cream body, black cap
     * and left ear, orange right ear and cheek, pink inner ears. */
    int sway = (tail % 5) - 2;
    stroke_cubic(c, ox, oy, n, d, 48, 108, 18, 104, 12, 72, 30 + sway * 4, 56, 8, c->cream);
    stroke_cubic(c, ox, oy, n, d, 44, 104, 22, 98, 18, 76, 32 + sway * 3, 64, 4, c->orange);
    stroke_cubic(c, ox, oy, n, d, 33, 66, 28, 56, 32, 48, 40 + sway * 2, 46, 4, c->black);
    ell(c, ox, oy, n, d, 92, 108, 38, 24, c->cream);
    ell(c, ox, oy, n, d, 72, 104, 16, 13, c->orange);
    ell(c, ox, oy, n, d, 114, 98, 14, 12, c->black);
    ell(c, ox, oy, n, d, 76, 126, 11, 7, c->cream);
    ell(c, ox, oy, n, d, 106, 126, 11, 7, c->cream);
    tri(c, ox, oy, n, d, 62, 58, 46, 16, 84, 46, c->black);
    tri(c, ox, oy, n, d, 64, 52, 54, 28, 76, 46, c->pink);
    tri(c, ox, oy, n, d, 122, 58, 140, 16, 102, 46, c->orange);
    tri(c, ox, oy, n, d, 120, 52, 130, 28, 108, 46, c->pink);
    ell(c, ox, oy, n, d, 92, 72, 32, 32, c->cream);
    ell(c, ox, oy, n, d, 80, 44, 22, 14, c->black);
    ell(c, ox, oy, n, d, 118, 98, 16, 12, c->orange);
    if (blink) {
        int y = oy + sc(76, n, d);
        int h = sc(3, n, d);
        if (h < 2) {
            h = 2;
        }
        fill_rect(c, ox + sc(71, n, d), y, sc(14, n, d), h, c->black);
        fill_rect(c, ox + sc(101, n, d), y, sc(14, n, d), h, c->black);
    } else {
        ell(c, ox, oy, n, d, 78, 76, 7, 8, c->black);
        ell(c, ox, oy, n, d, 108, 76, 7, 8, c->black);
        ell(c, ox, oy, n, d, 80, 73, 2, 3, c->white);
        ell(c, ox, oy, n, d, 110, 73, 2, 3, c->white);
    }
    ell(c, ox, oy, n, d, 93, 86, 4, 3, c->pink);
    line(c,
         ox + sc(85, n, d), oy + sc(92, n, d),
         ox + sc(93, n, d), oy + sc(97, n, d), c->black);
    line(c,
         ox + sc(93, n, d), oy + sc(97, n, d),
         ox + sc(101, n, d), oy + sc(92, n, d), c->black);
    line(c, ox + sc(68, n, d), oy + sc(82, n, d), ox + sc(46, n, d), oy + sc(76, n, d), c->black);
    line(c, ox + sc(68, n, d), oy + sc(88, n, d), ox + sc(46, n, d), oy + sc(90, n, d), c->black);
    line(c, ox + sc(116, n, d), oy + sc(82, n, d), ox + sc(138, n, d), oy + sc(76, n, d), c->black);
    line(c, ox + sc(116, n, d), oy + sc(88, n, d), ox + sc(138, n, d), oy + sc(90, n, d), c->black);
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
    clock_row(c, 2, 58, hhmm, c->white);
    if (scene->time_valid) {
        char date[20];
        date_line(scene, date, sizeof date);
        text_center(c, 62, 1, date, c->muted);
    }
    if (scene->place[0]) {
        text_center(c, 74, 2, scene->place, c->warm);
    }
    weather_pair(c, 96, 96, 2, scene->temp_c, scene->humidity);
    /* 2/3 of the web viewBox: about 107x93, same patches as the SVG. */
    kitten(c, 106, 118, 2, 3, 1, scene->tail);
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
    clock_at(c, 8, 6, 20, hhmm, c->white);
    weather_pair(c, 168, 8, 2, scene->temp_c, scene->humidity);
    kitten(c, 24, 40, 1, 2, scene->blink, scene->tail);
    text_center(c, 116, 2, "Calico", c->warm);
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
