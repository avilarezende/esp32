#pragma once

#include "esp_err.h"

/* Start the CYD panel task when CONFIG_APP_ENABLE_CYD is set. */
esp_err_t cyd_display_start(void);

/* Show live snapshots (about 1 per second) of a HA camera for `seconds`.
 * A tap closes the viewer early. Safe to call from any task. */
void cyd_display_show_camera(const char *entity, int seconds);

/* Panel colour options, saved in NVS. invert applies at once; bgr needs a
 * reboot. test=1 shows colour bars (red, green, blue, white, black) for 15 s. */
void cyd_display_set_color(int invert, int bgr, int test);
