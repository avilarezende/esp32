#include "cyd_splash.h"
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "calico_splash.inc"

#define STRIP 16
static uint16_t s_line[320 * STRIP];   /* DRAM buffer (see cyd_display.c note) */

void cyd_splash_play(esp_lcd_panel_handle_t panel, int w, int h, int loops)
{
    for (int i = 0; i < w * STRIP; i++) s_line[i] = 0xFFFF;   /* white */
    for (int y = 0; y < h; y += STRIP) {
        esp_lcd_panel_draw_bitmap(panel, 0, y, w, (y + STRIP > h) ? h : y + STRIP, s_line);
    }
    int x0 = (w - SPLASH_W) / 2, y0 = (h - SPLASH_H) / 2;
    for (int l = 0; l < loops; l++) {
        for (int f = 0; f < SPLASH_FRAMES; f++) {
            const uint8_t *px = SPLASH_PIX + (size_t)f * SPLASH_W * SPLASH_H;
            for (int y = 0; y < SPLASH_H; y += STRIP) {
                int rows = (y + STRIP > SPLASH_H) ? SPLASH_H - y : STRIP;
                for (int i = 0; i < SPLASH_W * rows; i++) s_line[i] = SPLASH_PAL[px[y * SPLASH_W + i]];
                esp_lcd_panel_draw_bitmap(panel, x0, y0 + y, x0 + SPLASH_W, y0 + y + rows, s_line);
            }
            vTaskDelay(pdMS_TO_TICKS(SPLASH_FRAME_MS));
        }
    }
}
