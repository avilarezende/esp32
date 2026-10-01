#pragma once

#include <stdint.h>

/* Landscape framebuffer of the ESP32-2432S028 (Cheap Yellow Display). */
#define CYD_W 320
#define CYD_H 240

#define CYD_UI_PORTAL 0
#define CYD_UI_HOME   1
#define CYD_UI_SAVER  2
#define CYD_UI_SLEEP  3

#define CYD_ROW_MAX 4

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
    int tail;   /* 0..5, shifts the tail */
    int row_count;
    cyd_row_t rows[CYD_ROW_MAX];
    char place[16];
    char status[20];
    char ip[20];
} cyd_scene_t;

/* Paint one horizontal band of the 320x240 scene into `band` (band_h rows). */
void cyd_scene_paint(uint16_t *band, int y0, int band_h, const cyd_scene_t *scene);
