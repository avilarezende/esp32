#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HA_DEVICE_MAX 4
#define HA_NAME_MAX   18
#define HA_DETAIL_MAX 16

typedef struct {
    char name[HA_NAME_MAX];
    char detail[HA_DETAIL_MAX];
    int on; /* 1 on/open, 0 off/closed, -1 other */
} ha_device_t;

typedef struct {
    int configured;   /* hub address saved */
    int ready;        /* 1 = last fetch ok, 0 = never, -1 = failed */
    int has_temp;
    int has_humidity;
    int temp_c;
    int humidity;
    int lights_on;
    int lights_total;
    int device_count;
    ha_device_t devices[HA_DEVICE_MAX];
    char place[16];
    char status[20];
} ha_snapshot_t;

/* Start the background poller (no-op when CYD is disabled). */
esp_err_t ha_client_start(void);

/* Copy the latest snapshot under the poller lock. */
void ha_client_copy(ha_snapshot_t *out);

#ifdef __cplusplus
}
#endif
