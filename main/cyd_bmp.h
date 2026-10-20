#pragma once

#include <stdint.h>

#include "cyd_scene.h"

/* 24-bit BMP, 320x240, no row padding. 54 + 320*240*3 = 230454. */
#define CYD_BMP_HEADER 54
#define CYD_BMP_ROW    (CYD_W * 3)
#define CYD_BMP_SIZE   (CYD_BMP_HEADER + CYD_BMP_ROW * CYD_H)

/* File header + DIB. Height is positive: rows are bottom-up. */
void cyd_bmp_header(uint8_t dst[CYD_BMP_HEADER]);

/* One BMP row (B, G, R) from host-endian RGB565. `dst` needs `width` * 3 bytes. */
void cyd_bmp_row(uint8_t *dst, const uint16_t *rgb565, int width);
