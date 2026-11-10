/*
 * Panel-id classification for the two CYD 2.8" boards. No hardware: the
 * bytes are what RDDID (0x04) and ILI9341 Read ID4 (0xD3) return, dummy
 * byte included.
 */

#include "cyd_panel.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_st7789_id_with_or_without_dummy(void)
{
    const uint8_t with_dummy[] = {0x00, 0x85, 0x85, 0x52};
    const uint8_t bare[] = {0x85, 0x85, 0x52, 0xff};
    uint32_t id = 0;

    TEST_ASSERT_EQUAL_INT(CYD_PANEL_ST7789, cyd_panel_from_ids(with_dummy, 4, NULL, 0, &id));
    TEST_ASSERT_EQUAL_UINT32(0x858552, id);
    TEST_ASSERT_EQUAL_INT(CYD_PANEL_ST7789, cyd_panel_from_ids(bare, 4, NULL, 0, &id));
    TEST_ASSERT_EQUAL_INT(CYD_PANEL_ST7789, cyd_panel_from_ids(NULL, 0, with_dummy, 4, &id));
}

static void test_ili9341_and_ili9342_ids(void)
{
    const uint8_t ili[] = {0x00, 0x00, 0x93, 0x41};
    const uint8_t ili2[] = {0x00, 0x93, 0x42, 0x00};
    uint32_t id = 1;

    TEST_ASSERT_EQUAL_INT(CYD_PANEL_ILI9341, cyd_panel_from_ids(ili, 4, NULL, 0, &id));
    TEST_ASSERT_EQUAL_UINT32(0x9341, id);
    TEST_ASSERT_EQUAL_INT(CYD_PANEL_ILI9342, cyd_panel_from_ids(NULL, 0, ili2, 4, &id));
    TEST_ASSERT_EQUAL_UINT32(0x9342, id);
}

static void test_unknown_or_partial_id_stays_on_ili9341(void)
{
    const uint8_t zeros[4] = {0};
    const uint8_t ones[4] = {0xff, 0xff, 0xff, 0xff};
    const uint8_t almost[] = {0x85, 0x85, 0x00};
    uint32_t id = 1;

    TEST_ASSERT_EQUAL_INT(CYD_PANEL_ILI9341, cyd_panel_from_ids(zeros, 4, zeros, 4, &id));
    TEST_ASSERT_EQUAL_UINT32(0, id);
    TEST_ASSERT_EQUAL_INT(CYD_PANEL_ILI9341, cyd_panel_from_ids(ones, 4, NULL, 0, &id));
    TEST_ASSERT_EQUAL_INT(CYD_PANEL_ILI9341, cyd_panel_from_ids(almost, 3, NULL, 0, &id));
    TEST_ASSERT_EQUAL_INT(CYD_PANEL_ILI9341, cyd_panel_from_ids(NULL, 0, NULL, 0, &id));
}

static void test_st7789_wins_over_a_stray_9341_byte_pair(void)
{
    const uint8_t mixed[] = {0x85, 0x85, 0x52, 0x93, 0x41};
    uint32_t id = 0;

    TEST_ASSERT_EQUAL_INT(CYD_PANEL_ST7789, cyd_panel_from_ids(mixed, 5, NULL, 0, &id));
    TEST_ASSERT_EQUAL_UINT32(0x858552, id);
}

static void test_saved_choice_beats_detection_and_auto_keeps_ili9341(void)
{
    TEST_ASSERT_EQUAL_INT(CYD_PANEL_ILI9341, cyd_panel_resolve(CYD_PANEL_AUTO, CYD_PANEL_ILI9341));
    TEST_ASSERT_EQUAL_INT(CYD_PANEL_ILI9341, cyd_panel_resolve(CYD_PANEL_AUTO, 0));
    TEST_ASSERT_EQUAL_INT(CYD_PANEL_ST7789, cyd_panel_resolve(CYD_PANEL_AUTO, CYD_PANEL_ST7789));
    TEST_ASSERT_EQUAL_INT(CYD_PANEL_ILI9342, cyd_panel_resolve(CYD_PANEL_AUTO, CYD_PANEL_ILI9342));
    TEST_ASSERT_EQUAL_INT(CYD_PANEL_ILI9341, cyd_panel_resolve(CYD_PANEL_ILI9341, CYD_PANEL_ST7789));
    TEST_ASSERT_EQUAL_INT(CYD_PANEL_ST7789, cyd_panel_resolve(CYD_PANEL_ST7789, CYD_PANEL_ILI9341));
    TEST_ASSERT_EQUAL_INT(CYD_PANEL_ILI9341, cyd_panel_resolve(9, CYD_PANEL_ILI9341));
    TEST_ASSERT_EQUAL_STRING("st7789", cyd_panel_name(CYD_PANEL_ST7789));
    TEST_ASSERT_EQUAL_STRING("ili9341", cyd_panel_name(CYD_PANEL_ILI9341));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_st7789_id_with_or_without_dummy);
    RUN_TEST(test_ili9341_and_ili9342_ids);
    RUN_TEST(test_unknown_or_partial_id_stays_on_ili9341);
    RUN_TEST(test_st7789_wins_over_a_stray_9341_byte_pair);
    RUN_TEST(test_saved_choice_beats_detection_and_auto_keeps_ili9341);
    return UNITY_END();
}
