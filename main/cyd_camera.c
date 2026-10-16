#include "cyd_camera.h"
#include <string.h>
#include "esp32/rom/tjpgd.h"
#include "esp_log.h"
#include "cyd_scene.h"

static const char *TAG = "cam";

typedef struct {
    const uint8_t *src;
    int len, pos;
    int crop_x, crop_y, off_x, off_y;
    esp_lcd_panel_handle_t panel;
} jctx_t;

static uint8_t s_work[3100];
static uint16_t s_blk[2][16 * 16 * 4];   /* scale 0 MCU is at most 16x16 */
static int s_pp;

static UINT in_fn(JDEC *jd, BYTE *buf, UINT n)
{
    jctx_t *c = (jctx_t *)jd->device;
    int left = c->len - c->pos;
    if ((int)n > left) n = (UINT)left;
    if (buf) memcpy(buf, c->src + c->pos, n);
    c->pos += (int)n;
    return n;
}

static UINT out_fn(JDEC *jd, void *bmp, JRECT *r)
{
    jctx_t *c = (jctx_t *)jd->device;
    int bw = r->right - r->left + 1;
    int x0 = r->left - c->crop_x + c->off_x, y0 = r->top - c->crop_y + c->off_y;
    int x1 = x0 + bw, y1 = y0 + (r->bottom - r->top + 1);
    int cx0 = x0 < 0 ? 0 : x0, cy0 = y0 < 0 ? 0 : y0;
    int cx1 = x1 > CYD_W ? CYD_W : x1, cy1 = y1 > CYD_H ? CYD_H : y1;
    if (cx0 >= cx1 || cy0 >= cy1) return 1;
    uint16_t *dst = s_blk[s_pp];
    s_pp ^= 1;
    const uint8_t *rgb = (const uint8_t *)bmp;
    int k = 0;
    for (int y = cy0; y < cy1; y++) {
        const uint8_t *p = rgb + ((y - y0) * bw + (cx0 - x0)) * 3;
        for (int x = cx0; x < cx1; x++, p += 3) {
            uint16_t v = (uint16_t)(((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3));
            dst[k++] = (uint16_t)((v << 8) | (v >> 8));
        }
    }
    esp_lcd_panel_draw_bitmap(c->panel, cx0, cy0, cx1, cy1, dst);
    return 1;
}

esp_err_t cyd_camera_draw(esp_lcd_panel_handle_t panel, const uint8_t *jpg, int len)
{
    JDEC jd;
    jctx_t c = { .src = jpg, .len = len, .panel = panel };
    JRESULT r = jd_prepare(&jd, in_fn, s_work, sizeof s_work, &c);
    if (r != JDR_OK) {
        ESP_LOGW(TAG, "jd_prepare %d", r);
        return ESP_FAIL;
    }
    uint8_t scale = 0;
    while (scale < 3 && (jd.width >> scale) > 640) scale++;
    int w = jd.width >> scale, h = jd.height >> scale;
    c.crop_x = w > CYD_W ? (w - CYD_W) / 2 : 0;
    c.crop_y = h > CYD_H ? (h - CYD_H) / 2 : 0;
    c.off_x = w < CYD_W ? (CYD_W - w) / 2 : 0;
    c.off_y = h < CYD_H ? (CYD_H - h) / 2 : 0;
    r = jd_decomp(&jd, out_fn, scale);
    if (r != JDR_OK) {
        ESP_LOGW(TAG, "jd_decomp %d", r);
        return ESP_FAIL;
    }
    return ESP_OK;
}
