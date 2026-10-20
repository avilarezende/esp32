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
    uint16_t bar;
    uint16_t card;
    uint16_t lift;
    uint16_t line;
    uint16_t cream;
    uint16_t orange;
    uint16_t black;
    uint16_t pink;
    uint16_t white;
    uint16_t warm;
    uint16_t cool;
    uint16_t muted;
    uint16_t faint;
    uint16_t ok;
    uint16_t warn;
    uint16_t crit;
    uint16_t info;
} canvas_t;

/* Status bar and the 8 px page grid. Touch targets in cyd_display.c stay put. */
#define BAR_H 24
#define WIFI_X (CYD_W - 24)
#define MARGIN 8

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

/* Filled rounded rect. Corners are a few clipped rows; the body is one rect. */
static void fill_round(canvas_t *c, int x, int y, int w, int h, int r, uint16_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    if (r < 1) {
        fill_rect(c, x, y, w, h, color);
        return;
    }
    if (r > w / 2) {
        r = w / 2;
    }
    if (r > h / 2) {
        r = h / 2;
    }
    if (r < 1) {
        fill_rect(c, x, y, w, h, color);
        return;
    }
    fill_rect(c, x, y + r, w, h - 2 * r, color);
    for (int i = 0; i < r; i++) {
        int dy = r - i;
        int span = r * r - dy * dy;
        int dx = 0;
        while ((dx + 1) * (dx + 1) <= span) {
            dx++;
        }
        int inset = r - dx;
        if (inset < 0) {
            inset = 0;
        }
        int rw = w - 2 * inset;
        if (rw <= 0) {
            continue;
        }
        fill_rect(c, x + inset, y + i, rw, 1, color);
        fill_rect(c, x + inset, y + h - 1 - i, rw, 1, color);
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

#include "cyd_cat.inc"

/* One source row of a palette sprite. Index 0 is transparent. */
static void cat_decode(const cat_part_t *p, int sy, uint8_t *dst)
{
    int a = CAT_ROW[p->row + sy];
    int b = CAT_ROW[p->row + sy + 1];
    int x = 0;
    int i = a;
    while (x < p->w) {
        dst[x++] = 0;
    }
    x = 0;
    while (i < b && x < p->w) {
        int op = CAT_RLE[i++];
        if (op < 128) {
            x += op;
        } else {
            int n = op - 128;
            while (n-- > 0 && x < p->w && i < b) {
                dst[x++] = CAT_RLE[i++];
            }
        }
    }
}

/* Nearest-neighbour scale. n/d is the size of the 96x88 stage. Only the
 * rows in this LCD band are decoded, into a stack row (no extra BSS). */
static void cat_blit(canvas_t *c, int part, int ox, int oy, int n, int d)
{
    const cat_part_t *p = &CAT_PART[part];
    int dw, dh, dx, dy, y, last;
    uint8_t row[96];
    if (n < 1) {
        n = 1;
    }
    if (d < 1) {
        d = 1;
    }
    if (p->w > 96 || p->h < 1 || p->w < 1) {
        return;
    }
    dw = p->w * n / d;
    dh = p->h * n / d;
    if (dw < 1 || dh < 1) {
        return;
    }
    dx = ox + p->x * n / d;
    dy = oy + p->y * n / d;
    if (dy + dh <= c->y0 || dy >= c->y0 + c->bh) {
        return;
    }
    last = -1;
    for (y = 0; y < dh; y++) {
        int sy_screen = dy + y;
        int sy, x;
        uint16_t *dst;
        if (sy_screen < c->y0 || sy_screen >= c->y0 + c->bh || sy_screen >= CYD_H) {
            continue;
        }
        sy = y * p->h / dh;
        if (sy >= p->h) {
            sy = p->h - 1;
        }
        if (sy != last) {
            cat_decode(p, sy, row);
            last = sy;
        }
        dst = c->band + (sy_screen - c->y0) * CYD_W;
        for (x = 0; x < dw; x++) {
            int sx = x * p->w / dw;
            int px;
            uint8_t ix;
            if (sx >= p->w) {
                sx = p->w - 1;
            }
            ix = row[sx];
            if (!ix || ix >= CAT_PAL_N) {
                continue;
            }
            px = dx + x;
            if ((unsigned)px >= CYD_W) {
                continue;
            }
            dst[px] = CAT_PAL[ix];
        }
    }
}

/* Breath, tail and blink share one clock. Salt slides the windows so two
 * visits to the same pose do not blink on the same tick. Phase 0, salt 0
 * is eyes open, tail neutral-left, ears still: the host tests depend on it. */
static int cat_tick(const cyd_scene_t *sc)
{
    int t = sc->phase + sc->cat_salt * 3;
    if (t < 0) {
        t = 0;
    }
    return t;
}

static int cat_breath(int t)
{
    int q = t % 12;
    int up = q < 6 ? q : 12 - q;
    return up / 3; /* 0..2, slow, held at the top */
}

static int cat_tail_part(int t)
{
    int q = (t / 2) % 10;
    int up = q < 5 ? q : 10 - q;
    if (up > 4) {
        up = 4;
    }
    return CAT_TAIL0 + up;
}

static int cat_ear_part(int t)
{
    int e = t % 37;
    if (e == 33 || e == 34) {
        return CAT_EARS_LEFT;
    }
    if (e == 35 || e == 36) {
        return CAT_EARS_RIGHT;
    }
    return CAT_EARS_SIT;
}

static int cat_eye_part(int t, int base)
{
    int b = t % 28;
    if (b == 24) {
        return CAT_EYES_HALF;
    }
    if (b == 25) {
        return CAT_EYES_SHUT;
    }
    if (b == 26) {
        return CAT_EYES_HALF;
    }
    return base;
}

static void cat_sit(canvas_t *c, int ox, int oy, int n, int d, int tail, int ears, int eyes, int mouth)
{
    cat_blit(c, tail, ox, oy, n, d);
    cat_blit(c, ears, ox, oy, n, d);
    cat_blit(c, CAT_BODY, ox, oy, n, d);
    cat_blit(c, eyes, ox, oy, n, d);
    cat_blit(c, mouth, ox, oy, n, d);
}

/* ox,oy is the top-left of the 96x88 stage. n/d scales that stage. */
static void kitten_act(canvas_t *c, int ox, int oy, int n, int d, const cyd_scene_t *sc_)
{
    int t = cat_tick(sc_);
    int breath = cat_breath(t);
    int top, bot, dy, eyes, ears;
    if (n < 1) {
        n = 1;
    }
    if (d < 1) {
        d = 1;
    }
    top = oy - 2;
    bot = oy + CAT_H * n / d + 3;
    if (bot < c->y0 || top >= c->y0 + c->bh) {
        return;
    }
    dy = breath * n / d;
    switch (sc_->behavior) {
    case CYD_CAT_SLEEP: {
        int z = (t / 4) % 3;
        cat_blit(c, CAT_SLEEP0 + z, ox, oy + dy, n, d);
        return;
    }
    case CYD_CAT_STRETCH: {
        /* 16-frame ease out and back. Frame 0 is the sitting cat, so the
         * pose leaves and returns without a pop. */
        int span = 16;
        int p = sc_->phase;
        int up, f;
        if (p < 0) {
            p = 0;
        }
        if (p > span) {
            p = span;
        }
        up = p < span / 2 ? p : span - p;
        f = up * 5 / 8;
        if (f <= 0) {
            cat_sit(c, ox, oy + dy, n, d, cat_tail_part(t), CAT_EARS_SIT, CAT_EYES_HALF, CAT_MOUTH_SMILE);
        } else {
            cat_blit(c, CAT_STRETCH0 + f - 1, ox, oy, n, d);
        }
        return;
    }
    case CYD_CAT_PURR: {
        int wob = (t & 2) ? n / d : 0;
        eyes = cat_eye_part(t, CAT_EYES_HAPPY);
        cat_sit(c, ox + wob, oy + dy, n, d, cat_tail_part(t), CAT_EARS_SIT, eyes, CAT_MOUTH_PURR);
        return;
    }
    case CYD_CAT_ALERT:
        eyes = cat_eye_part(t, CAT_EYES_UP);
        ears = CAT_EARS_ALERT;
        dy -= n / d;
        if (dy > 0) {
            dy = 0;
        }
        cat_sit(c, ox, oy + dy, n, d, cat_tail_part(t), ears, eyes, CAT_MOUTH_SMILE);
        return;
    default: {
        int look = (t / 9) % 6;
        int base = CAT_EYES_C;
        if (look == 2) {
            base = CAT_EYES_R;
        } else if (look == 4) {
            base = CAT_EYES_L;
        }
        eyes = cat_eye_part(t, base);
        cat_sit(c, ox, oy + dy, n, d, cat_tail_part(t), cat_ear_part(t), eyes, CAT_MOUTH_SMILE);
        return;
    }
    }
}

static void icon_gate(canvas_t *c, int x, int y, uint16_t col)
{
    fill_rect(c, x, y, 3, 22, col);
    fill_rect(c, x + 33, y, 3, 22, col);
    fill_rect(c, x + 3, y + 2, 30, 2, col);
    fill_rect(c, x + 3, y + 18, 30, 2, col);
    for (int i = 0; i < 4; i++) {
        fill_rect(c, x + 7 + i * 7, y + 2, 2, 16, col);
    }
}

static void icon_cam(canvas_t *c, int x, int y, uint16_t col, uint16_t hole)
{
    fill_round(c, x + 2, y + 4, 32, 16, 3, col);
    fill_rect(c, x + 10, y + 1, 10, 4, col);
    fill_disc(c, x + 18, y + 12, 5, hole);
    fill_disc(c, x + 18, y + 12, 2, col);
}

static void cam_button(canvas_t *c)
{
    int x = CYD_CAMBTN_X, y = CYD_CAMBTN_Y, w = CYD_CAMBTN_W, h = CYD_CAMBTN_H;
    fill_round(c, x, y, w, h, 8, c->lift);
    const cyd_cfg_t *cfg = cyd_cfg();
    int ix = x + (w - 36) / 2;
    if (cfg->btn_icon == 1) {
        icon_cam(c, ix, y + 2, c->warm, c->lift);
    } else {
        icon_gate(c, ix, y + 3, c->warm);
    }
    const char *lb = cfg->btn_label[0] ? cfg->btn_label : "Porta";
    int tw = text_px(lb, 1);
    int tx = x + (w - tw) / 2;
    if (tx < x + 2) {
        tx = x + 2;
    }
    text(c, tx, y + h - 12, 1, lb, c->white);
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

/* Four bars. Green: internet. Slash: no Wi-Fi or Wi-Fi without the check.
 * Grey bars and no slash: the check has not run yet. */
static void wifi_icon(canvas_t *c, int x, int y, int state)
{
    uint16_t on = state == 2 ? c->ok : (state == 3 ? c->faint : c->muted);
    static const uint8_t bh[4] = {3, 6, 9, 12};
    for (int i = 0; i < 4; i++) {
        int h = bh[i];
        uint16_t col = on;
        if (state == 0) {
            col = c->faint;
        } else if (state == 1) {
            col = i >= 2 ? on : c->faint;
        }
        fill_rect(c, x + 1 + i * 4, y + (12 - h), 3, h, col);
    }
    if (state == 0 || state == 1) {
        line(c, x + 1, y + 1, x + 15, y + 13, c->crit);
        line(c, x, y + 2, x + 14, y + 14, c->crit);
    }
}

static uint16_t level_col(canvas_t *c, int level)
{
    return level >= 2 ? c->crit : (level == 1 ? c->warn : c->info);
}

static uint16_t status_col(canvas_t *c, const char *s)
{
    if (!s || !s[0]) {
        return c->muted;
    }
    if (strcmp(s, "HA ok") == 0) {
        return c->ok;
    }
    if (strcmp(s, "aguardando") == 0) {
        return c->muted;
    }
    return c->warn;
}

static void paint_bar(canvas_t *c)
{
    fill_rect(c, 0, 0, CYD_W, BAR_H, c->bar);
    fill_rect(c, 0, BAR_H, CYD_W, 1, c->line);
}

static void paint_wifi(canvas_t *c, int state)
{
    wifi_icon(c, WIFI_X, 5, state);
}

static int badge_width(const cyd_scene_t *s)
{
    char t[16];
    int n;
    if (s->alert_count <= 0) {
        return 0;
    }
    n = s->alert_count > 99 ? 99 : s->alert_count;
    snprintf(t, sizeof t, "! %d", n);
    int w = text_px(t, 1) + 12;
    return w < 28 ? 28 : w;
}

static void draw_badge(canvas_t *c, const cyd_scene_t *s, int x)
{
    char t[16];
    int tw;
    int w;
    int lvl;
    int n;
    if (s->alert_count <= 0) {
        return;
    }
    n = s->alert_count > 99 ? 99 : s->alert_count;
    snprintf(t, sizeof t, "! %d", n);
    tw = text_px(t, 1);
    w = tw + 12;
    if (w < 28) {
        w = 28;
    }
    lvl = s->alert_rows > 0 ? s->alerts[0].level : 0;
    fill_round(c, x, 4, w, 16, 8, level_col(c, lvl));
    text(c, x + (w - tw) / 2, 8, 1, t, c->white);
}

static void paint_title_bar(canvas_t *c, const char *title, const char *hint, int net)
{
    int title_end;
    paint_bar(c);
    text(c, MARGIN, 4, 2, title, c->white);
    paint_wifi(c, net);
    title_end = MARGIN + text_px(title, 2) + 8;
    if (hint && hint[0]) {
        int hw = text_px(hint, 1);
        int x = WIFI_X - 8 - hw;
        if (x >= title_end) {
            text(c, x, 8, 1, hint, c->muted);
        }
    }
}

static void alert_banner(canvas_t *c, const cyd_scene_t *s)
{
    const cyd_alert_row_t *a;
    if (!s->banner || s->alert_rows <= 0) {
        return;
    }
    a = &s->alerts[0];
    fill_round(c, MARGIN, 192, CYD_W - 2 * MARGIN, 42, 8, level_col(c, a->level));
    text(c, MARGIN + 12, 198, 1, a->title, c->white);
    text(c, MARGIN + 12, 216, 1, a->msg, c->white);
}

static void paint_alerts(canvas_t *c, const cyd_scene_t *s)
{
    int y = 32;
    int card_h = s->updates > 0 ? 34 : 42;
    int stride = card_h + 6;
    paint_title_bar(c, "Alertas", "toque = visto", s->net_state);
    if (s->updates > 0) {
        char u[40];
        snprintf(u, sizeof u, "%d atualizacao(oes) no HA", s->updates);
        fill_round(c, MARGIN, y, CYD_W - 2 * MARGIN, 20, 6, c->card);
        text(c, MARGIN + 10, y + 6, 1, u, c->warm);
        y += 26;
    }
    if (s->alert_rows == 0 && s->updates == 0) {
        text_center(c, 96, 1, "nenhum alerta", c->muted);
    }
    for (int i = 0; i < s->alert_rows && i < 3 && y + card_h <= 176; i++) {
        int title_y = y + (card_h >= 40 ? 6 : 4);
        int msg_y = y + (card_h >= 40 ? 24 : 18);
        fill_round(c, MARGIN, y, CYD_W - 2 * MARGIN, card_h, 8, c->card);
        fill_rect(c, MARGIN + 8, y + 8, 4, card_h - 16, level_col(c, s->alerts[i].level));
        text(c, MARGIN + 20, title_y, 1, s->alerts[i].title, c->white);
        text(c, MARGIN + 20, msg_y, 1, s->alerts[i].msg, c->muted);
        y += stride;
    }
    /* Touch: ty >= 180 dismisses everything. The button fills that strip. */
    fill_round(c, MARGIN, 180, CYD_W - 2 * MARGIN, 52, 10, c->crit);
    text_center(c, 198, 2, "LIMPAR TUDO", c->white);
}

static void paint_portal(canvas_t *c, const cyd_scene_t *s)
{
    paint_title_bar(c, "Portal", NULL, s->net_state);
    fill_round(c, 12, 36, CYD_W - 24, 188, 12, c->card);
    text_center(c, 48, 2, "Conecte no Wi-Fi", c->white);
    text_center(c, 76, 3, "ESP32-Setup", c->warm);
    text_center(c, 112, 1, "senha", c->muted);
    text_center(c, 128, 3, "esp32setup", c->white);
    text_center(c, 168, 1, "Abra no celular", c->muted);
    text_center(c, 188, 2, "192.168.4.1", c->cool);
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

static void format_hhmm(const cyd_scene_t *scene, char out[6])
{
    if (scene->time_valid) {
        out[0] = (char)('0' + scene->hour / 10);
        out[1] = (char)('0' + scene->hour % 10);
        out[2] = ':';
        out[3] = (char)('0' + scene->minute / 10);
        out[4] = (char)('0' + scene->minute % 10);
        out[5] = '\0';
    } else {
        memcpy(out, "--:--", 6);
    }
}

static void home_weather(const cyd_scene_t *scene, char left[8], char right[8])
{
    memcpy(left, "--c", 4);
    memcpy(right, "--%", 4);
    if (scene->has_temp && scene->has_humidity) {
        weather_text(left, right, scene->temp_c, scene->humidity);
    } else if (scene->has_temp) {
        weather_text(left, right, scene->temp_c, 0);
        memcpy(right, "--%", 4);
    } else if (scene->has_humidity) {
        weather_text(left, right, 0, scene->humidity);
        memcpy(left, "--c", 4);
    }
}

static void metric_card(canvas_t *c, int x, int y, int w, int h, const char *value, uint16_t col)
{
    int vw = text_px(value, 2);
    fill_round(c, x, y, w, h, 8, c->card);
    text(c, x + (w - vw) / 2, y + (h - 16) / 2, 2, value, col);
}

static void paint_home_bar(canvas_t *c, const cyd_scene_t *scene, const char *hhmm)
{
    int right = WIFI_X - 6;
    int bw;
    paint_bar(c);
    aa_text(c, MARGIN, 2, 0, hhmm, c->white, 1);
    paint_wifi(c, scene->net_state);
    bw = badge_width(scene);
    if (bw) {
        draw_badge(c, scene, 96);
    }
    if (scene->status[0]) {
        int tw = text_px(scene->status, 1);
        int w = tw + 12;
        int x = right - w;
        int min_x = bw ? 96 + bw + 6 : 96;
        if (x < min_x) {
            x = min_x;
        }
        if (x + w <= right) {
            fill_round(c, x, 4, w, 16, 8, c->lift);
            text(c, x + 6, 8, 1, scene->status, status_col(c, scene->status));
        }
    }
}

static void paint_empty(canvas_t *c, const cyd_scene_t *scene)
{
    if (strcmp(scene->status, "sem hub") == 0) {
        text_center(c, 108, 1, "configure o Home Assistant", c->muted);
        text_center(c, 126, 1, "no celular (portal / Ajustes)", c->muted);
    } else if (strcmp(scene->status, "offline") == 0 ||
               strcmp(scene->status, "token ruim") == 0 ||
               strcmp(scene->status, "sem token") == 0) {
        text_center(c, 108, 1, "nao foi possivel ler o HA", c->muted);
        text_center(c, 126, 1, "confira endereco e token", c->muted);
    } else if (strcmp(scene->status, "HA ok") == 0) {
        text_center(c, 108, 1, "nenhuma luz ou tomada no HA", c->muted);
    } else if (strcmp(scene->status, "aguardando") == 0) {
        text_center(c, 108, 1, "buscando dispositivos...", c->muted);
    } else {
        text_center(c, 108, 1, "erro ao ler o HA:", c->muted);
        text_center(c, 126, 1, scene->status, c->muted);
    }
    if (scene->cat_on) {
        kitten_act(c, 124, 150, 3, 4, scene);
    }
}

static void paint_rows(canvas_t *c, const cyd_scene_t *scene)
{
    int n = scene->row_count;
    int y = 90;
    int row_h = 22;
    int h;
    int x = MARGIN;
    int w = CYD_W - 2 * MARGIN;
    if (n > CYD_ROW_MAX) {
        n = CYD_ROW_MAX;
    }
    h = 8 + n * row_h + 4;
    if (y + h > 214) {
        h = 214 - y;
    }
    fill_round(c, x, y, w, h, 8, c->card);
    for (int i = 0; i < n; i++) {
        const cyd_row_t *r = &scene->rows[i];
        int ry = y + 8 + i * row_h;
        uint16_t det = r->on == 1 ? c->ok : (r->on == 0 ? c->muted : c->cool);
        uint16_t dot = r->on == 1 ? c->ok : (r->on == 0 ? c->faint : c->cool);
        int dw = text_px(r->detail, 1);
        fill_disc(c, x + 12, ry + 4, 3, dot);
        text(c, x + 20, ry, 1, r->name, c->white);
        text(c, x + w - dw - 10, ry, 1, r->detail, det);
        if (i + 1 < n) {
            fill_rect(c, x + 12, ry + 16, w - 24, 1, c->line);
        }
    }
}

static void paint_home(canvas_t *c, const cyd_scene_t *scene)
{
    char hhmm[6];
    const char *place = scene->place[0] ? scene->place : "Casa";
    int avail = CYD_CAMBTN_X - 8 - MARGIN;
    int gap = 8;
    int cw;
    format_hhmm(scene, hhmm);
    paint_home_bar(c, scene, hhmm);

    if (text_px(place, 2) <= CYD_CAMBTN_X - 16) {
        text(c, MARGIN, 30, 2, place, c->white);
    } else {
        text(c, MARGIN, 34, 1, place, c->white);
    }

    cw = (avail - gap) / 2;
    if (scene->has_temp || scene->has_humidity) {
        char left[8];
        char right[8];
        home_weather(scene, left, right);
        metric_card(c, MARGIN, 52, cw, 30, left, c->warm);
        metric_card(c, MARGIN + cw + gap, 52, cw, 30, right, c->cool);
    } else {
        fill_round(c, MARGIN, 52, avail, 30, 8, c->card);
        text(c, MARGIN + 12, 63, 1, "clima indisponivel", c->muted);
    }

    if (scene->row_count <= 0) {
        paint_empty(c, scene);
    } else {
        paint_rows(c, scene);
        /* The banner covers this corner. Drawing the cat anyway leaves a
         * few pixels sticking out under the rounded alert card. */
        if (scene->cat_on && !scene->banner) {
            kitten_act(c, 272, 204, 5, 12, scene);
        }
    }

    text_center(c, 222, 1, "40 min sem toque: tela off", c->muted);
    if (scene->cam_btn) {
        cam_button(c);
    }
    alert_banner(c, scene);
}

static void paint_saver_bar(canvas_t *c, const cyd_scene_t *scene)
{
    paint_bar(c);
    if (scene->alert_count > 0) {
        draw_badge(c, scene, MARGIN);
    }
    if (scene->time_valid) {
        char date[20];
        date_line(scene, date, sizeof date);
        text_center(c, 8, 1, date, c->muted);
    }
    paint_wifi(c, scene->net_state);
}

static void paint_glance_card(canvas_t *c, const cyd_scene_t *scene)
{
    int x = 16;
    int y = 86;
    int w = CYD_W - 32;
    int h = 40;
    char left[8];
    char right[8];
    int lw;
    int rw;
    int gap = 14;
    int pair;
    int ty;
    weather_text(left, right, scene->temp_c, scene->humidity);
    lw = text_px(left, 2);
    rw = text_px(right, 2);
    pair = lw + gap + rw;
    ty = y + (h - 16) / 2;
    fill_round(c, x, y, w, h, 8, c->card);
    if (scene->place[0]) {
        int scale = 2;
        int pw = text_px(scene->place, 2);
        int room = w - 28 - pair;
        int rx = x + w - 12 - pair;
        if (pw > room) {
            scale = 1;
            pw = text_px(scene->place, 1);
        }
        text(c, x + 12, y + (h - 8 * scale) / 2, scale, scene->place, c->white);
        if (x + 12 + pw + 8 < rx) {
            fill_rect(c, rx - 8, y + 10, 1, h - 20, c->line);
        }
        text(c, rx, ty, 2, left, c->warm);
        text(c, rx + lw + gap, ty, 2, right, c->cool);
    } else {
        int rx = x + (w - pair) / 2;
        text(c, rx, ty, 2, left, c->warm);
        text(c, rx + lw + gap, ty, 2, right, c->cool);
    }
}

static void paint_saver(canvas_t *c, const cyd_scene_t *scene)
{
    char hhmm[6];
    format_hhmm(scene, hhmm);
    paint_saver_bar(c, scene);
    aa_center(c, 32, 1, hhmm, c->white);
    paint_glance_card(c, scene);
    if (scene->cat_on) {
        /* Native 96x88 sprite, centred under the weather card. */
        kitten_act(c, 112, 132, 1, 1, scene);
    }
    text_center(c, 226, 1, "toque na tela", c->muted);
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
        .bg = rgb565(10, 14, 24),
        .bar = rgb565(18, 26, 42),
        .card = rgb565(26, 36, 58),
        .lift = rgb565(36, 48, 74),
        .line = rgb565(46, 60, 88),
        .cream = rgb565(246, 241, 232),
        .orange = rgb565(239, 138, 54),
        .black = rgb565(42, 36, 31),
        .pink = rgb565(243, 166, 184),
        .white = rgb565(244, 247, 251),
        .warm = rgb565(242, 176, 108),
        .cool = rgb565(126, 198, 228),
        .muted = rgb565(154, 168, 190),
        .faint = rgb565(96, 110, 132),
        .ok = rgb565(52, 199, 142),
        .warn = rgb565(232, 164, 74),
        .crit = rgb565(224, 92, 92),
        .info = rgb565(96, 148, 232),
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
        paint_portal(&c, scene);
    } else if (scene->mode == CYD_UI_SAVER) {
        paint_saver(&c, scene);
    } else if (scene->mode == CYD_UI_ALERTS) {
        paint_alerts(&c, scene);
    } else {
        paint_home(&c, scene);
    }
}

