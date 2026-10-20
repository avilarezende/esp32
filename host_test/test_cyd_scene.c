/*
 * Host paint checks for the CYD scene. No panel: the same band painter the
 * firmware uses, asserted on flat regions (status bar, cards, button, sleep).
 */

#include <string.h>

#include "cyd_cfg.h"
#include "cyd_scene.h"
#include "unity.h"

static cyd_cfg_t s_cfg = {
    .btn_icon = 0,
    .btn_label = "Porta",
    .cat = 1,
};

const cyd_cfg_t *cyd_cfg(void)
{
    return &s_cfg;
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static uint16_t bg, bar, card, lift, cream, warm, ok, crit, warn;

static uint16_t frame[CYD_W * CYD_H];
static uint16_t band[CYD_W * 20];

void setUp(void)
{
    s_cfg.btn_icon = 0;
    memcpy(s_cfg.btn_label, "Porta", 6);
    bg = rgb565(10, 14, 24);
    bar = rgb565(18, 26, 42);
    card = rgb565(26, 36, 58);
    lift = rgb565(36, 48, 74);
    cream = rgb565(246, 241, 232);
    warm = rgb565(242, 176, 108);
    ok = rgb565(52, 199, 142);
    crit = rgb565(224, 92, 92);
    warn = rgb565(232, 164, 74);
}

void tearDown(void) {}

static void paint(const cyd_scene_t *scene)
{
    memset(frame, 0xA5, sizeof frame);
    for (int y = 0; y < CYD_H; y += 20) {
        cyd_scene_paint(band, y, 20, scene);
        memcpy(frame + y * CYD_W, band, (size_t)CYD_W * 20 * sizeof(uint16_t));
    }
}

static uint16_t px(int x, int y)
{
    return frame[y * CYD_W + x];
}

static int count_eq(uint16_t color)
{
    int n = 0;
    for (int i = 0; i < CYD_W * CYD_H; i++) {
        if (frame[i] == color) {
            n++;
        }
    }
    return n;
}

static void base_scene(cyd_scene_t *s)
{
    memset(s, 0, sizeof *s);
    s->time_valid = 1;
    s->hour = 9;
    s->minute = 35;
    s->wday = 2;
    s->mday = 20;
    s->month = 9;
    s->temp_c = 29;
    s->humidity = 64;
    s->has_temp = 1;
    s->has_humidity = 1;
    s->cat_on = 1;
    s->net_state = 2;
    s->cam_btn = 1;
    memcpy(s->place, "Casa", 5);
    memcpy(s->status, "HA ok", 6);
}

static void test_status_bar_is_shared(void)
{
    cyd_scene_t s;
    uint16_t bar_px;

    base_scene(&s);
    s.mode = CYD_UI_PORTAL;
    s.net_state = 0;
    paint(&s);
    bar_px = px(2, 8);
    TEST_ASSERT_EQUAL_UINT16(bar, bar_px);
    TEST_ASSERT_EQUAL_UINT16(bg, px(4, 30));
    TEST_ASSERT_EQUAL_UINT16(card, px(20, 40));
    TEST_ASSERT_TRUE(count_eq(warm) > 20);

    base_scene(&s);
    s.mode = CYD_UI_HOME;
    paint(&s);
    TEST_ASSERT_EQUAL_UINT16(bar_px, px(2, 8));
    TEST_ASSERT_EQUAL_UINT16(bg, px(4, 30));
    TEST_ASSERT_EQUAL_UINT16(lift, px(256, 40));
    TEST_ASSERT_TRUE(count_eq(warm) > 10);
    TEST_ASSERT_TRUE(count_eq(ok) > 8);

    base_scene(&s);
    s.mode = CYD_UI_SAVER;
    paint(&s);
    TEST_ASSERT_EQUAL_UINT16(bar_px, px(2, 8));
    TEST_ASSERT_EQUAL_UINT16(bg, px(4, 30));
    TEST_ASSERT_EQUAL_UINT16(card, px(24, 100));
    TEST_ASSERT_TRUE(count_eq(cream) > 80);

    base_scene(&s);
    s.mode = CYD_UI_ALERTS;
    s.alert_rows = 1;
    s.alerts[0].level = 2;
    memcpy(s.alerts[0].title, "Portao", 7);
    memcpy(s.alerts[0].msg, "aberto", 7);
    paint(&s);
    TEST_ASSERT_EQUAL_UINT16(bar_px, px(2, 8));
    TEST_ASSERT_EQUAL_UINT16(crit, px(20, 188));
}

static void test_home_rows_card_and_banner(void)
{
    cyd_scene_t s;
    base_scene(&s);
    s.mode = CYD_UI_HOME;
    s.row_count = 4;
    memcpy(s.rows[0].name, "Garagem 1", 10);
    memcpy(s.rows[0].detail, "ligado", 7);
    s.rows[0].on = 1;
    memcpy(s.rows[1].name, "Garagem 2", 10);
    memcpy(s.rows[1].detail, "desligado", 10);
    s.rows[1].on = 0;
    memcpy(s.rows[2].name, "Piscina", 8);
    memcpy(s.rows[2].detail, "ligado", 7);
    s.rows[2].on = 1;
    memcpy(s.rows[3].name, "Poco", 5);
    memcpy(s.rows[3].detail, "12.4", 5);
    s.rows[3].on = -1;
    s.alert_count = 2;
    s.alert_rows = 1;
    s.banner = 1;
    s.alerts[0].level = 1;
    memcpy(s.alerts[0].title, "Campainha", 10);
    memcpy(s.alerts[0].msg, "frente", 7);
    paint(&s);
    TEST_ASSERT_EQUAL_UINT16(card, px(40, 94));
    TEST_ASSERT_EQUAL_UINT16(warn, px(280, 210));

    s.banner = 0;
    s.alert_count = 0;
    paint(&s);
    TEST_ASSERT_TRUE(count_eq(cream) > 10);
}

static void test_sleep_and_bars_and_stable(void)
{
    cyd_scene_t s;
    static uint16_t again[CYD_W * CYD_H];
    base_scene(&s);
    s.mode = CYD_UI_SLEEP;
    paint(&s);
    TEST_ASSERT_EQUAL_INT(CYD_W * CYD_H, count_eq(0));

    s.mode = CYD_UI_HOME;
    s.test_bars = 1;
    paint(&s);
    TEST_ASSERT_EQUAL_UINT16(rgb565(255, 0, 0), px(10, 10));
    TEST_ASSERT_EQUAL_UINT16(rgb565(0, 255, 0), px(10, 60));

    s.test_bars = 0;
    s.mode = CYD_UI_SAVER;
    paint(&s);
    memcpy(again, frame, sizeof frame);
    paint(&s);
    TEST_ASSERT_EQUAL_INT(0, memcmp(again, frame, sizeof frame));
}

static void test_cat_poses_keep_the_calico(void)
{
    cyd_scene_t s;
    static uint16_t idle[CYD_W * CYD_H];
    int poses[4] = {CYD_CAT_SLEEP, CYD_CAT_STRETCH, CYD_CAT_PURR, CYD_CAT_ALERT};
    int phases[4] = {4, 8, 2, 0};
    int i;

    base_scene(&s);
    s.mode = CYD_UI_SAVER;
    s.behavior = CYD_CAT_IDLE;
    s.phase = 4;
    paint(&s);
    TEST_ASSERT_TRUE(count_eq(cream) > 80);
    memcpy(idle, frame, sizeof idle);

    for (i = 0; i < 4; i++) {
        s.behavior = poses[i];
        s.phase = phases[i];
        paint(&s);
        TEST_ASSERT_TRUE(count_eq(cream) > 40);
        TEST_ASSERT_TRUE(memcmp(idle, frame, sizeof idle) != 0);
    }
}

static void test_empty_home_keeps_the_cat(void)
{
    cyd_scene_t s;
    base_scene(&s);
    s.mode = CYD_UI_HOME;
    s.has_temp = 0;
    s.has_humidity = 0;
    memcpy(s.status, "sem hub", 8);
    paint(&s);
    TEST_ASSERT_EQUAL_UINT16(card, px(126, 58));
    TEST_ASSERT_TRUE(count_eq(cream) > 40);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_status_bar_is_shared);
    RUN_TEST(test_home_rows_card_and_banner);
    RUN_TEST(test_sleep_and_bars_and_stable);
    RUN_TEST(test_cat_poses_keep_the_calico);
    RUN_TEST(test_empty_home_keeps_the_cat);
    return UNITY_END();
}
