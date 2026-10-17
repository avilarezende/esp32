#pragma once

#include <stddef.h>
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
