#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
/* Decode a baseline JPEG (ROM TJpgDec) and draw it centred/cropped on the
 * 320x240 panel. Larger than 640 px wide images are scaled down by 2/4/8. */
esp_err_t cyd_camera_draw(esp_lcd_panel_handle_t panel, const uint8_t *jpg, int len);
