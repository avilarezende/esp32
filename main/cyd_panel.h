#pragma once

#include <stdint.h>

/* Which glass the one firmware drives. Saved in NVS as `c_panel`.
 * 0 reads the panel ID and only leaves ILI9341 when the ID is not a known
 * other controller, so a board that never saved this key keeps today's
 * ILI9341 init. */
enum {
    CYD_PANEL_AUTO = 0,
    CYD_PANEL_ILI9341 = 1,
    CYD_PANEL_ST7789 = 2,
    CYD_PANEL_ILI9342 = 3,
};

/* RDDID (0x04) and ILI9341 Read ID4 (0xD3), including the dummy byte the
 * controller clocks out first. ST7789 answers 0x85 0x85 0x52. ILI9341
 * answers 0x93 0x41, ILI9342 answers 0x93 0x42. Anything else, including a
 * failed read, is reported as ILI9341 so a flaky MISO cannot retarget the
 * original board. *id_out receives the matched value (0x858552, 0x9341,
 * 0x9342) or 0 when nothing matched. */
int cyd_panel_from_ids(const uint8_t *id04, int n04, const uint8_t *idd3, int nd3,
                       uint32_t *id_out);

/* `saved` is the NVS choice. A specific choice wins. Auto uses `detected`
 * only for ST7789 and ILI9342. */
int cyd_panel_resolve(int saved, int detected);

const char *cyd_panel_name(int panel);
