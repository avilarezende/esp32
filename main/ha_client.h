#pragma once

#include <stddef.h>
#include <stdint.h>
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

/* Send `text` to Home Assistant's conversation API (POST
 * /api/conversation/process, language pt-BR) with the saved hub address and
 * long-lived token, and copy the spoken reply into `out` (plain UTF-8).
 * Always writes a human-readable message, also on errors. Keeps the
 * conversation_id between calls so follow-ups have context. */
/* GET /api/camera_proxy/<entity>?width=320&height=240 into a malloc'd buffer
 * (caller frees). */
esp_err_t ha_client_fetch_camera(const char *entity, uint8_t **jpg, int *len);

/* Shared key for the /camera and /display endpoints: first 16 hex chars of
 * SHA-256(HA token). Empty string when no token is saved. */
void ha_client_device_key(char out[17]);

void ha_client_conversation(const char *text, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
