#pragma once
#include "esp_lcd_panel_ops.h"
/* Play the animated Calico splash (white background, centred) once. */
void cyd_splash_play(esp_lcd_panel_handle_t panel, int w, int h, int loops);
