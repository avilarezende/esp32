#pragma once

#include <stdint.h>

/* Landscape framebuffer of the ESP32-2432S028 (Cheap Yellow Display). */
#define CYD_W 320
#define CYD_H 240

#define CYD_UI_PORTAL 0
#define CYD_UI_HOME   1
#define CYD_UI_SAVER  2
#define CYD_UI_SLEEP  3
#define CYD_UI_ALERTS 4

/* Cat pose. Idle blinks, breathes, sways its tail and sometimes flicks an ear.
 * Sleep is the night screensaver. Stretch yawns. Purr is a touch. Alert is a
 * new Home Assistant notification (and an occasional look around). */
#define CYD_CAT_IDLE    0
#define CYD_CAT_SLEEP   1
#define CYD_CAT_STRETCH 2
#define CYD_CAT_PURR    3
#define CYD_CAT_ALERT   4

#define CYD_ALERT_ROWS 4
typedef struct {
    char title[32];
    char msg[48];
    int level; /* 0 info, 1 aviso, 2 critico */
} cyd_alert_row_t;

#define CYD_ROW_MAX 5

typedef struct {
    char name[18];
    char detail[16];
    int on; /* 1 on, 0 off, -1 other */
} cyd_row_t;

typedef struct {
    int mode;
    int time_valid;
    int hour;
    int minute;
    int wday;   /* 0 = Sunday */
    int mday;
    int month;  /* 0 = January */
    int temp_c;
    int humidity;
    int has_temp;
    int has_humidity;
    int blink;
    int tail;   /* kept; the painter drives the tail from phase */
    int row_count;
    cyd_row_t rows[CYD_ROW_MAX];
    char place[16];
    char status[20];
    char ip[20];
    int cam_btn;     /* draw the "frente direita" camera button (home) */
    int behavior;    /* CYD_CAT_IDLE .. CYD_CAT_ALERT */
    int phase;       /* animation step inside the behaviour */
    int cat_salt;    /* shifts blink/ear timing so cycles are not identical */
    int test_bars;   /* colour test pattern */
    int net_state;   /* 0 no Wi-Fi, 1 Wi-Fi without internet, 2 internet ok, 3 unknown */
    int cat_on;
    int alert_count; /* badge (alerts + updates) */
    int updates;
    int banner;      /* show newest alert banner */
    cyd_alert_row_t alerts[CYD_ALERT_ROWS];
    int alert_rows;
} cyd_scene_t;

/* Camera quick button on the home screen (touch target, screen coords). */
#define CYD_CAMBTN_X 252
#define CYD_CAMBTN_Y 26
#define CYD_CAMBTN_W 62
#define CYD_CAMBTN_H 46

/* Alerts badge sits in the status bar. Opening the list is not this rect:
 * display_task treats the whole top strip (and the bottom banner) as the
 * target, so the pill can move without changing the touch behaviour. */
#define CYD_BADGE_X 196
#define CYD_BADGE_X_SAVER 4
#define CYD_BADGE_Y 0
#define CYD_BADGE_W 52
#define CYD_BADGE_H 24

/* Paint one horizontal band of the 320x240 scene into `band` (band_h rows). */
void cyd_scene_paint(uint16_t *band, int y0, int band_h, const cyd_scene_t *scene);
