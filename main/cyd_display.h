#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* Start the CYD panel task when CONFIG_APP_ENABLE_CYD is set. */
esp_err_t cyd_display_start(void);

/* Show live snapshots (about 1 per second) of a HA camera for `seconds`.
 * A tap closes the viewer early. Safe to call from any task. */
void cyd_display_show_camera(const char *entity, int seconds);

/* Panel colour options, saved in NVS. invert and brightness are applied on
 * the next display_task pass (not from the HTTP task); bgr needs a reboot.
 * test=1 shows colour bars (red, green, blue, white, black) for 15 s. */
void cyd_display_set_color(int invert, int bgr, int test);

/* Ask display_task to re-apply cyd_cfg() (inversion, brightness). */
void cyd_display_apply_cfg(void);

/* Alerts shown as banner + badge; level 0 info, 1 warning, 2 critical;
 * secs 0 = until dismissed. Same id replaces. dismiss(NULL/"") clears all. */
void cyd_alert_push(const char *id, const char *title, const char *msg, int level, int secs);
void cyd_alert_dismiss(const char *id);
void cyd_alerts_json(char *buf, size_t n);
void cyd_touch_json(char *buf, size_t n);

/* SPI color transfers the ILI9341 panel IO may have in flight
 * (`trans_queue_depth`). `esp_lcd_panel_draw_bitmap` returns before they
 * finish, so a buffer is only safe to rewrite after `cyd_lcd_drain` or after
 * this many newer draws have themselves been drained. */
#define CYD_LCD_TRANS_QUEUE 10

/* Block until queued panel color transfers finish. Display task only. */
void cyd_lcd_drain(void);

/* On-demand screenshot. The HTTP handler only enqueues; display_task is the
 * only code that reads the panel. Bands come back bottom-row-first as RGB565
 * in a shared buffer of a few KB. recv: 1 = band, 0 = frame done, -1 = fail.
 * ack() returns the buffer to display_task. end() after every successful begin(). */
#define CYD_SHOT_TIMEOUT_MS 3000
esp_err_t cyd_display_shot_begin(void);
int cyd_display_shot_recv(const uint16_t **rgb565, int *rows, int timeout_ms);
void cyd_display_shot_ack(void);
void cyd_display_shot_end(void);
