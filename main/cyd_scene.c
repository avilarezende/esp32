#include <stdio.h>
#include "cyd_scene.h"
#include "cyd_cfg.h"

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

__attribute__((unused)) static void clock_row(canvas_t *c, int y, int h, const char *hhmm, uint16_t color)
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


/* ---- Anti-aliased clock (Montserrat SemiBold, 4-bit alpha) ---- */
#include "clock_font.inc"

static uint16_t blend565(uint16_t bg, uint16_t fg, int a /*0..15*/)
{
    int br = bg >> 11, bgc = (bg >> 5) & 63, bb = bg & 31;
    int fr = fg >> 11, fgc = (fg >> 5) & 63, fb = fg & 31;
    int r = br + (fr - br) * a / 15, g = bgc + (fgc - bgc) * a / 15, b = bb + (fb - bb) * a / 15;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static int aa_index(char ch)
{
    for (int i = 0; CF_CHARS[i]; i++) if (CF_CHARS[i] == ch) return i;
    return -1;
}

static int aa_text(canvas_t *c, int x, int y, int big, const char *s, uint16_t color, int draw)
{
    const uint16_t *off = big ? CF_BIG_OFF : CF_SMALL_OFF;
    const uint8_t *wv = big ? CF_BIG_W : CF_SMALL_W, *adv = big ? CF_BIG_ADV : CF_SMALL_ADV;
    const uint8_t *pix = big ? CF_BIG_PIX : CF_SMALL_PIX;
    int H = big ? CF_BIG_H : CF_SMALL_H;
    int x0 = x;
    for (; *s; s++) {
        int gi = aa_index(*s);
        if (gi < 0) { x += adv[0] / 2; continue; }
        int w = wv[gi], ox = (adv[gi] - w) / 2;
        if (draw) {
            for (int yy = 0; yy < H; yy++) {
                int sy = y + yy - c->y0;
                if (sy < 0 || sy >= c->bh) continue;
                for (int xx = 0; xx < w; xx++) {
                    int sx = x + ox + xx;
                    if (sx < 0 || sx >= CYD_W) continue;
                    int i = yy * w + xx;
                    uint8_t byte = pix[off[gi] + i / 2];
                    int a = (i & 1) ? (byte & 15) : (byte >> 4);
                    if (!a) continue;
                    uint16_t *d = &c->band[sy * CYD_W + sx];
                    *d = a == 15 ? color : blend565(*d, color, a);
                }
            }
        }
        x += adv[gi];
    }
    return x - x0;
}

static void aa_center(canvas_t *c, int y, int big, const char *s, uint16_t color)
{
    int w = aa_text(c, 0, y, big, s, color, 0);
    aa_text(c, (CYD_W - w) / 2, y, big, s, color, 1);
}

static int s_pdx; /* pupil shift for "look around" */
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
        ell(c, ox, oy, n, d, 78 + s_pdx, 76, 7, 8, c->black);
        ell(c, ox, oy, n, d, 108 + s_pdx, 76, 7, 8, c->black);
        ell(c, ox, oy, n, d, 80 + s_pdx, 73, 2, 3, c->white);
        ell(c, ox, oy, n, d, 110 + s_pdx, 73, 2, 3, c->white);
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


static int tri_wave(int p, int period, int amp)
{
    int q = p % period, h = period / 2;
    return (q < h ? q : period - q) * amp / h;
}

/* The always-visible cat: several behaviours chosen at random by the display task. */
static void kitten_act(canvas_t *c, int ox, int oy, int n, int d, const cyd_scene_t *sc_)
{
    int p = sc_->phase;
    s_pdx = 0;
    switch (sc_->behavior) {
    case 1: { /* sleeping: eyes shut, slow breathing, rising z's */
        int breath = tri_wave(p, 16, 3);
        kitten(c, ox, oy + sc(4 + breath, n, d), n, d, 1, 0);
        int zy = oy + sc(30, n, d) - (p % 12) * 2;
        text(c, ox + sc(140, n, d), zy, 1, "z", c->cool);
        if ((p % 12) > 5) text(c, ox + sc(152, n, d), zy - 10, n >= 1 && d <= 2 ? 2 : 1, "Z", c->cool);
        break; }
    case 2: { /* stretching: front paws slide forward, eyes closed at full stretch */
        int ext = tri_wave(p, 24, 26);
        kitten(c, ox, oy + sc(ext / 6, n, d), n, d, ext > 18, p);
        ell(c, ox, oy, n, d, 62 - ext, 132, 18 + ext / 2, 6, c->cream);
        ell(c, ox, oy, n, d, 44 - ext * 3 / 2, 132, 5, 5, c->pink);
        ell(c, ox, oy, n, d, 120 + ext / 2, 132, 14 + ext / 3, 6, c->cream);
        break; }
    case 3: { /* pawing / tapping the glass */
        kitten(c, ox, oy, n, d, (p % 16) >= 15, p);
        int lift = tri_wave(p, 8, 30);
        ell(c, ox, oy, n, d, 70, 112 - lift, 10, 8, c->cream);
        ell(c, ox, oy, n, d, 70, 108 - lift, 4, 3, c->pink);
        if (lift == 0) {
            int cx = ox + sc(70, n, d), cy = oy + sc(100, n, d), r = sc(16, n, d);
            line(c, cx - r, cy - r / 2, cx - r / 2, cy - r / 4, c->warm);
            line(c, cx + r / 2, cy - r / 4, cx + r, cy - r / 2, c->warm);
            line(c, cx, cy - r, cx, cy - r / 2, c->warm);
        }
        break; }
    case 4: /* looking around */
        s_pdx = (p / 6) % 3 == 0 ? -4 : ((p / 6) % 3 == 1 ? 0 : 4);
        kitten(c, ox, oy, n, d, (p % 20) >= 19, p);
        break;
    default: /* idle: blink + tail sway */
        kitten(c, ox, oy, n, d, (p % 16) >= 14, sc_->tail);
        break;
    }
    s_pdx = 0;
}

static void cam_button(canvas_t *c)
{
    int x = CYD_CAMBTN_X, y = CYD_CAMBTN_Y, w = CYD_CAMBTN_W, h = CYD_CAMBTN_H;
    fill_rect(c, x, y, w, h, c->muted);
    fill_rect(c, x + 2, y + 2, w - 4, h - 4, c->bg);
    const cyd_cfg_t *cfg = cyd_cfg();
    if (cfg->btn_icon == 1) {
        fill_rect(c, x + 14, y + 10, 26, 16, c->warm);
        fill_tri(c, x + 40, y + 18, x + 50, y + 11, x + 50, y + 25, c->warm);
        fill_disc(c, x + 27, y + 18, 4, c->bg);
    } else {
        /* gate (portao): two posts, top/bottom rails, vertical bars */
        fill_rect(c, x + 12, y + 6, 4, 24, c->warm);
        fill_rect(c, x + w - 16, y + 6, 4, 24, c->warm);
        fill_rect(c, x + 16, y + 10, w - 32, 2, c->warm);
        fill_rect(c, x + 16, y + 26, w - 32, 2, c->warm);
        for (int bx = x + 20; bx < x + w - 18; bx += 5) fill_rect(c, bx, y + 10, 2, 18, c->warm);
    }
    const char *lb = cfg->btn_label[0] ? cfg->btn_label : "Porta";
    text(c, x + (w - text_px(lb, 1)) / 2, y + h - 14, 1, lb, c->white);
}

static void test_bars(canvas_t *c)
{
    static const char *lbl[5] = {"VERMELHO", "VERDE", "AZUL", "BRANCO", "PRETO"};
    uint16_t col[5] = {rgb565(255, 0, 0), rgb565(0, 255, 0), rgb565(0, 0, 255), rgb565(255, 255, 255), 0};
    for (int i = 0; i < 5; i++) {
        fill_rect(c, 0, i * 48, CYD_W, 48, col[i]);
        text(c, 10, i * 48 + 16, 2, lbl[i], i == 3 ? 0 : rgb565(255, 255, 255));
    }
}


/* ---- Connectivity icon, alerts badge/banner/list ---- */
static void arc_pts(canvas_t *c, int cx, int cy, int r, int th, uint16_t col)
{
    for (int dy = -r - th; dy <= 0; dy++) {
        for (int dx = -r - th; dx <= r + th; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 >= r * r && d2 <= (r + th) * (r + th) && -dy >= (dx < 0 ? -dx : dx)) plot(c, cx + dx, cy + dy, col);
        }
    }
}

static void wifi_icon(canvas_t *c, int x, int y, int state)
{
    uint16_t ok = rgb565(34, 197, 94), dim = rgb565(71, 85, 105), red = rgb565(239, 68, 68);
    uint16_t col = state == 2 ? ok : (state == 3 ? c->muted : dim);
    int cx = x + 14, cy = y + 20;
    arc_pts(c, cx, cy, 4, 2, col);
    arc_pts(c, cx, cy, 9, 2, col);
    arc_pts(c, cx, cy, 14, 2, col);
    fill_disc(c, cx, cy, 2, col);
    if (state == 0 || state == 1) {
        for (int t = -1; t <= 1; t++) {
            line(c, x + 16 + t, y + 8, x + 28 + t, y + 20, red);
            line(c, x + 28 + t, y + 8, x + 16 + t, y + 20, red);
        }
    }
}

static uint16_t level_col(canvas_t *c, int level)
{
    (void)c;
    return level >= 2 ? rgb565(220, 38, 38) : (level == 1 ? rgb565(217, 119, 6) : rgb565(37, 99, 235));
}

static void alert_badge(canvas_t *c, const cyd_scene_t *s, int bx)
{
    if (s->alert_count <= 0) return;
    char t[12];
    snprintf(t, sizeof t, "! %d", s->alert_count > 99 ? 99 : s->alert_count);
    int lvl = s->alert_rows > 0 ? s->alerts[0].level : 0;
    fill_rect(c, bx, CYD_BADGE_Y + 2, CYD_BADGE_W, CYD_BADGE_H - 4, level_col(c, lvl));
    text(c, bx + (CYD_BADGE_W - text_px(t, 1)) / 2, CYD_BADGE_Y + 8, 1, t, c->white);
}

static void alert_banner(canvas_t *c, const cyd_scene_t *s)
{
    if (!s->banner || s->alert_rows <= 0) return;
    const cyd_alert_row_t *a = &s->alerts[0];
    fill_rect(c, 0, 196, CYD_W, 44, level_col(c, a->level));
    text(c, 6, 202, 1, a->title, c->white);
    text(c, 6, 220, 1, a->msg, c->white);
}

static void paint_alerts(canvas_t *c, const cyd_scene_t *s)
{
    text(c, 8, 8, 2, "Alertas", c->white);
    text(c, CYD_W - text_px("toque = visto", 1) - 8, 12, 1, "toque = visto", c->muted);
    fill_rect(c, 8, 34, CYD_W - 16, 1, c->muted);
    int y = 40;
    if (s->updates > 0) {
        char u[40];
        snprintf(u, sizeof u, "%d atualizacao(oes) no HA", s->updates);
        text(c, 12, y + 4, 1, u, c->warm);
        y += 24;
    }
    if (s->alert_rows == 0 && s->updates == 0) text_center(c, 110, 1, "nenhum alerta", c->muted);
    for (int i = 0; i < s->alert_rows && i < 3 && y < 176; i++, y += 44) {
        fill_rect(c, 8, y, 4, 38, level_col(c, s->alerts[i].level));
        text(c, 18, y + 4, 1, s->alerts[i].title, c->white);
        text(c, 18, y + 22, 1, s->alerts[i].msg, c->muted);
    }
    fill_rect(c, 8, 184, CYD_W - 16, 50, rgb565(220, 38, 38));
    text_center(c, 196, 2, "LIMPAR TUDO", c->white);
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
    aa_center(c, 6, 1, hhmm, c->white);
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
    if (scene->cat_on) kitten_act(c, 106, 118, 2, 3, scene);
    text_center(c, 224, 1, "toque na tela", c->muted);
    wifi_icon(c, CYD_W - 36, 2, scene->net_state);
    alert_badge(c, scene, 4);
    alert_banner(c, scene);
}

static void paint_home(canvas_t *c, const cyd_scene_t *scene)
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
    aa_text(c, 8, 5, 0, hhmm, c->white, 1);
    if (scene->status[0]) {
        int sw = text_px(scene->status, 1);
        text(c, CYD_W - sw - 8, 8, 1, scene->status, c->cool);
    }

    const char *place = scene->place[0] ? scene->place : "Casa";
    text(c, 8, 32, 2, place, c->warm);

    if (scene->has_temp || scene->has_humidity) {
        char left[8] = "--c";
        char right[8] = "--%";
        if (scene->has_temp) {
            weather_text(left, right, scene->temp_c, scene->has_humidity ? scene->humidity : 0);
            if (!scene->has_humidity) {
                right[0] = '-';
                right[1] = '-';
                right[2] = '%';
                right[3] = '\0';
            }
        } else if (scene->has_humidity) {
            weather_text(left, right, 0, scene->humidity);
            left[0] = '-';
            left[1] = '-';
            left[2] = 'c';
            left[3] = '\0';
        }
        text(c, 8, 56, 2, left, c->warm);
        text(c, 100, 56, 2, right, c->cool);
    } else {
        text(c, 8, 56, 1, "clima indisponivel", c->muted);
    }

    fill_rect(c, 8, 80, CYD_W - 16, 1, c->muted);

    if (scene->row_count <= 0) {
        if (strcmp(scene->status, "sem hub") == 0) {
            text_center(c, 110, 1, "configure o Home Assistant", c->muted);
            text_center(c, 128, 1, "no celular (portal / Ajustes)", c->muted);
        } else if (strcmp(scene->status, "offline") == 0 ||
                   strcmp(scene->status, "token ruim") == 0 ||
                   strcmp(scene->status, "sem token") == 0) {
            text_center(c, 110, 1, "nao foi possivel ler o HA", c->muted);
            text_center(c, 128, 1, "confira endereco e token", c->muted);
        } else if (strcmp(scene->status, "HA ok") == 0) {
            text_center(c, 110, 1, "nenhuma luz ou tomada no HA", c->muted);
        } else if (strcmp(scene->status, "aguardando") == 0) {
            text_center(c, 110, 1, "buscando dispositivos...", c->muted);
        } else {
            text_center(c, 110, 1, "erro ao ler o HA:", c->muted);
            text_center(c, 128, 1, scene->status, c->muted);
        }
        if (scene->cat_on) kitten_act(c, 112, 148, 1, 2, scene);
    } else {
        for (int i = 0; i < scene->row_count && i < CYD_ROW_MAX; i++) {
            int y = 88 + i * 22;
            const cyd_row_t *r = &scene->rows[i];
            uint16_t name_c = c->white;
            uint16_t det_c = c->muted;
            if (r->on == 1) {
                det_c = c->warm;
            } else if (r->on == 0) {
                det_c = c->muted;
            } else {
                det_c = c->cool;
            }
            text(c, 8, y, 1, r->name, name_c);
            int dw = text_px(r->detail, 1);
            text(c, CYD_W - dw - 8, y, 1, r->detail, det_c);
        }
    }

    text_center(c, 224, 1, "40 min sem toque: tela off", c->muted);
    if (scene->row_count > 0) {
        if (scene->cat_on) kitten_act(c, 268, 200, 1, 4, scene);
    }
    if (scene->cam_btn) {
        cam_button(c);
    }
    alert_badge(c, scene, CYD_BADGE_X);
    alert_banner(c, scene);
}

static void paint_sleep(canvas_t *c)
{
    for (int i = 0; i < c->bh * CYD_W; i++) {
        c->band[i] = 0;
    }
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
    if (scene->mode == CYD_UI_SLEEP) {
        paint_sleep(&c);
        return;
    }
    clear_band(&c);
    if (scene->test_bars) {
        test_bars(&c);
        return;
    }
    if (scene->mode == CYD_UI_PORTAL) {
        paint_portal(&c);
    } else if (scene->mode == CYD_UI_SAVER) {
        paint_saver(&c, scene);
    } else if (scene->mode == CYD_UI_ALERTS) {
        paint_alerts(&c, scene);
    } else {
        paint_home(&c, scene);
    }
}
