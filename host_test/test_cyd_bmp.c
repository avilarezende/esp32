/*
 * Host checks for the screenshot BMP: 54-byte header, 320x240, 230454 bytes,
 * bottom-up 24-bit layout and RGB565 expansion. No hardware.
 */

#include <string.h>

#include "unity.h"
#include "cyd_bmp.h"

void setUp(void) {}
void tearDown(void) {}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void test_header_is_320x240_24bit(void)
{
    uint8_t h[CYD_BMP_HEADER];
    cyd_bmp_header(h);

    TEST_ASSERT_EQUAL_UINT(54, CYD_BMP_HEADER);
    TEST_ASSERT_EQUAL_UINT(230454, CYD_BMP_SIZE);
    TEST_ASSERT_EQUAL_UINT(230454, 54 + 320 * 240 * 3);

    TEST_ASSERT_EQUAL_UINT8('B', h[0]);
    TEST_ASSERT_EQUAL_UINT8('M', h[1]);
    TEST_ASSERT_EQUAL_UINT32(230454, le32(h + 2));
    TEST_ASSERT_EQUAL_UINT32(54, le32(h + 10));
    TEST_ASSERT_EQUAL_UINT32(40, le32(h + 14));
    TEST_ASSERT_EQUAL_UINT32(320, le32(h + 18));
    TEST_ASSERT_EQUAL_UINT32(240, le32(h + 22)); /* positive height: bottom-up */
    TEST_ASSERT_EQUAL_UINT8(1, h[26]);
    TEST_ASSERT_EQUAL_UINT8(24, h[28]);
    TEST_ASSERT_EQUAL_UINT32(0, le32(h + 30));             /* BI_RGB */
    TEST_ASSERT_EQUAL_UINT32(320 * 240 * 3, le32(h + 34));
}

static void test_row_expands_rgb565_to_bgr24(void)
{
    uint16_t px[4] = { 0xF800, 0x07E0, 0x001F, 0xFFFF };
    uint8_t row[12];
    cyd_bmp_row(row, px, 4);

    /* red, green, blue, white — BMP channel order is B, G, R */
    TEST_ASSERT_EQUAL_UINT8(0x00, row[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00, row[1]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, row[2]);

    TEST_ASSERT_EQUAL_UINT8(0x00, row[3]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, row[4]);
    TEST_ASSERT_EQUAL_UINT8(0x00, row[5]);

    TEST_ASSERT_EQUAL_UINT8(0xFF, row[6]);
    TEST_ASSERT_EQUAL_UINT8(0x00, row[7]);
    TEST_ASSERT_EQUAL_UINT8(0x00, row[8]);

    TEST_ASSERT_EQUAL_UINT8(0xFF, row[9]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, row[10]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, row[11]);
}

static void test_black_and_mid_grey(void)
{
    uint16_t px[2] = { 0x0000, 0x8410 }; /* r=16 g=32 b=16 */
    uint8_t row[6];
    cyd_bmp_row(row, px, 2);

    TEST_ASSERT_EQUAL_UINT8(0x00, row[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00, row[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00, row[2]);
    /* expand5(16) = 132, expand6(32) = 130 */
    TEST_ASSERT_EQUAL_UINT8(132, row[3]);
    TEST_ASSERT_EQUAL_UINT8(130, row[4]);
    TEST_ASSERT_EQUAL_UINT8(132, row[5]);
}

static void test_read_chunk_fits_spi_fifo(void)
{
    /* 64-byte FIFO, 1 dummy byte, RGB666: 21 px, and 320 px is 16 bursts. */
    TEST_ASSERT_EQUAL_INT(21, cyd_lcd_read_px(CYD_LCD_READ_FIFO));
    TEST_ASSERT_TRUE(1 + 21 * 3 <= CYD_LCD_READ_FIFO);

    int chunks = 0;
    int rest = 0;
    for (int x = 0; x < CYD_W; ) {
        int n = CYD_W - x;
        int px = cyd_lcd_read_px(CYD_LCD_READ_FIFO);
        if (n > px) {
            n = px;
        }
        TEST_ASSERT_TRUE(1 + n * 3 <= CYD_LCD_READ_FIFO);
        x += n;
        rest = n;
        chunks++;
    }
    TEST_ASSERT_EQUAL_INT(16, chunks);
    TEST_ASSERT_EQUAL_INT(5, rest);
    TEST_ASSERT_TRUE(1 + 5 * 3 <= CYD_LCD_READ_FIFO);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_header_is_320x240_24bit);
    RUN_TEST(test_row_expands_rgb565_to_bgr24);
    RUN_TEST(test_black_and_mid_grey);
    RUN_TEST(test_read_chunk_fits_spi_fifo);
    return UNITY_END();
}
