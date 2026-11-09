#include "cyd_panel.h"

#include <string.h>

static int has_seq(const uint8_t *p, int n, const uint8_t *sig, int sn)
{
    if (!p || n < sn || sn <= 0) {
        return 0;
    }
    for (int i = 0; i <= n - sn; i++) {
        if (memcmp(p + i, sig, (size_t)sn) == 0) {
            return 1;
        }
    }
    return 0;
}

int cyd_panel_from_ids(const uint8_t *id04, int n04, const uint8_t *idd3, int nd3,
                       uint32_t *id_out)
{
    static const uint8_t st7789[] = {0x85, 0x85, 0x52};
    static const uint8_t ili9342[] = {0x93, 0x42};
    static const uint8_t ili9341[] = {0x93, 0x41};
    uint32_t id = 0;
    int panel = CYD_PANEL_ILI9341;

    if (has_seq(id04, n04, st7789, 3) || has_seq(idd3, nd3, st7789, 3)) {
        id = 0x858552;
        panel = CYD_PANEL_ST7789;
    } else if (has_seq(id04, n04, ili9342, 2) || has_seq(idd3, nd3, ili9342, 2)) {
        id = 0x9342;
        panel = CYD_PANEL_ILI9342;
    } else if (has_seq(id04, n04, ili9341, 2) || has_seq(idd3, nd3, ili9341, 2)) {
        id = 0x9341;
        panel = CYD_PANEL_ILI9341;
    }

    if (id_out) {
        *id_out = id;
    }
    return panel;
}

int cyd_panel_resolve(int saved, int detected)
{
    if (saved == CYD_PANEL_ILI9341 || saved == CYD_PANEL_ST7789 || saved == CYD_PANEL_ILI9342) {
        return saved;
    }
    if (detected == CYD_PANEL_ST7789 || detected == CYD_PANEL_ILI9342) {
        return detected;
    }
    return CYD_PANEL_ILI9341;
}

const char *cyd_panel_name(int panel)
{
    switch (panel) {
    case CYD_PANEL_AUTO:
        return "auto";
    case CYD_PANEL_ST7789:
        return "st7789";
    case CYD_PANEL_ILI9342:
        return "ili9342";
    default:
        return "ili9341";
    }
}
