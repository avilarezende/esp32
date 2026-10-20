#include "cyd_bmp.h"

#include <string.h>

_Static_assert(CYD_W == 320 && CYD_H == 240, "screenshot is the CYD landscape");
_Static_assert(CYD_BMP_ROW % 4 == 0, "24-bit rows need no padding");
_Static_assert(CYD_BMP_SIZE == 230454, "320x240 24-bit BMP is 230454 bytes");

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

void cyd_bmp_header(uint8_t dst[CYD_BMP_HEADER])
{
    memset(dst, 0, CYD_BMP_HEADER);
    dst[0] = 'B';
    dst[1] = 'M';
    put_le32(dst + 2, CYD_BMP_SIZE);
    put_le32(dst + 10, CYD_BMP_HEADER);
    put_le32(dst + 14, 40);
    put_le32(dst + 18, CYD_W);
    put_le32(dst + 22, CYD_H);          /* positive: bottom-up */
    dst[26] = 1;                        /* planes */
    dst[28] = 24;                       /* bits per pixel */
    put_le32(dst + 34, CYD_BMP_ROW * CYD_H);
}

static uint8_t expand5(unsigned v)
{
    return (uint8_t)((v << 3) | (v >> 2));
}

static uint8_t expand6(unsigned v)
{
    return (uint8_t)((v << 2) | (v >> 4));
}

void cyd_bmp_row(uint8_t *dst, const uint16_t *rgb565, int width)
{
    for (int x = 0; x < width; x++) {
        uint16_t c = rgb565[x];
        unsigned r = (c >> 11) & 0x1F;
        unsigned g = (c >> 5) & 0x3F;
        unsigned b = c & 0x1F;
        dst[x * 3 + 0] = expand5(b);
        dst[x * 3 + 1] = expand6(g);
        dst[x * 3 + 2] = expand5(r);
    }
}
