#include "cyd_ble.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

static const char *TAG = "cyd_ble";

/* StickS3: interval 320 ms, window 30 ms, passive. Units are 0.625 ms. */
#define SCAN_ITVL 512
#define SCAN_WINDOW 48
#define DEV_MAX 12
#define ADV_NAME_MAX 18

/* Gold Plus TCD-06B (Spock), public address as stored in HA. */
static const uint8_t SPOCK_MAC[6] = {0x00, 0x33, 0x7A, 0xDF, 0xF3, 0xE1};

typedef struct {
    uint8_t mac[6];
    int8_t rssi;
    char name[ADV_NAME_MAX];
    int64_t last_us;
    int used;
} dev_t;

static SemaphoreHandle_t s_mu;
static dev_t s_devs[DEV_MAX];
static dev_t s_spock;
static int s_scanning;
static int s_started;
static uint32_t s_adv;

static void mac_from_nimble(uint8_t out[6], const uint8_t val[6])
{
    for (int i = 0; i < 6; i++) {
        out[i] = val[5 - i];
    }
}

static int mac_eq(const uint8_t a[6], const uint8_t b[6])
{
    return memcmp(a, b, 6) == 0;
}

static void mac_str(char *out, size_t n, const uint8_t m[6])
{
    snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X",
             m[0], m[1], m[2], m[3], m[4], m[5]);
}

static void copy_name(char *dst, const uint8_t *src, int len)
{
    int n = 0;
    if (len > ADV_NAME_MAX - 1) {
        len = ADV_NAME_MAX - 1;
    }
    for (int i = 0; i < len; i++) {
        unsigned char c = src[i];
        if (c < 32 || c > 126 || c == '"' || c == '\\') {
            c = '?';
        }
        dst[n++] = (char)c;
    }
    dst[n] = 0;
}

static void remember(const uint8_t mac[6], int8_t rssi, const char *name)
{
    int64_t now = esp_timer_get_time();
    if (xSemaphoreTake(s_mu, pdMS_TO_TICKS(20)) != pdTRUE) {
        return;
    }
    s_adv++;
    if (mac_eq(mac, SPOCK_MAC)) {
        memcpy(s_spock.mac, mac, 6);
        s_spock.rssi = rssi;
        s_spock.last_us = now;
        s_spock.used = 1;
        if (name && name[0]) {
            strncpy(s_spock.name, name, ADV_NAME_MAX - 1);
            s_spock.name[ADV_NAME_MAX - 1] = 0;
        }
    }
    int slot = -1, oldest = 0;
    int64_t oldest_us = now;
    for (int i = 0; i < DEV_MAX; i++) {
        if (s_devs[i].used && mac_eq(s_devs[i].mac, mac)) {
            slot = i;
            break;
        }
        if (!s_devs[i].used) {
            slot = i;
            break;
        }
        if (s_devs[i].last_us < oldest_us) {
            oldest_us = s_devs[i].last_us;
            oldest = i;
        }
    }
    if (slot < 0) {
        slot = oldest;
    }
    memcpy(s_devs[slot].mac, mac, 6);
    s_devs[slot].rssi = rssi;
    s_devs[slot].last_us = now;
    s_devs[slot].used = 1;
    if (name && name[0]) {
        strncpy(s_devs[slot].name, name, ADV_NAME_MAX - 1);
        s_devs[slot].name[ADV_NAME_MAX - 1] = 0;
    }
    xSemaphoreGive(s_mu);
}

static int start_scan(void);

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type == BLE_GAP_EVENT_DISC) {
        uint8_t mac[6];
        char name[ADV_NAME_MAX];
        struct ble_hs_adv_fields fields;
        mac_from_nimble(mac, event->disc.addr.val);
        name[0] = 0;
        if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) == 0) {
            if (fields.name && fields.name_len > 0) {
                copy_name(name, fields.name, fields.name_len);
            }
        }
        remember(mac, event->disc.rssi, name);
        return 0;
    }
    if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        ESP_LOGW(TAG, "scan ended reason=%d, restarting", event->disc_complete.reason);
        s_scanning = 0;
        start_scan();
    }
    return 0;
}

static int start_scan(void)
{
    struct ble_gap_disc_params dp;
    int rc;
    memset(&dp, 0, sizeof(dp));
    dp.itvl = SCAN_ITVL;
    dp.window = SCAN_WINDOW;
    dp.passive = 1;
    dp.filter_duplicates = 0;
    rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &dp, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
        s_scanning = 0;
        return rc;
    }
    s_scanning = 1;
    ESP_LOGI(TAG, "passive scan started (interval 320ms, window 30ms)");
    return 0;
}

static void on_reset(int reason)
{
    s_scanning = 0;
    ESP_LOGW(TAG, "nimble reset, reason=%d", reason);
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "no BLE address: %d", rc);
        return;
    }
    start_scan();
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t cyd_ble_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    s_mu = xSemaphoreCreateMutex();
    if (!s_mu) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
    s_started = 1;
    ESP_LOGI(TAG, "BLE scanner started, heap=%u min=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size());
    return ESP_OK;
}

static int write_dev(char *buf, size_t n, const dev_t *d, int64_t now)
{
    char mac[18];
    int age = 0;
    mac_str(mac, sizeof(mac), d->mac);
    if (d->last_us > 0 && now >= d->last_us) {
        age = (int)((now - d->last_us) / 1000000);
    }
    return snprintf(buf, n,
                    "{\"mac\":\"%s\",\"rssi\":%d,\"age_s\":%d,\"name\":\"%s\"}",
                    mac, (int)d->rssi, age, d->name[0] ? d->name : "");
}

void cyd_ble_json(char *buf, size_t n)
{
    int64_t now = esp_timer_get_time();
    size_t o = 0;
    int scanning = 0, spock_used = 0, count = 0;
    dev_t spock, devs[DEV_MAX];
    uint32_t adv = 0;

    memset(&spock, 0, sizeof(spock));
    memset(devs, 0, sizeof(devs));
    if (s_mu && xSemaphoreTake(s_mu, pdMS_TO_TICKS(50)) == pdTRUE) {
        scanning = s_scanning;
        adv = s_adv;
        spock = s_spock;
        spock_used = s_spock.used;
        for (int i = 0; i < DEV_MAX; i++) {
            devs[i] = s_devs[i];
            if (s_devs[i].used) {
                count++;
            }
        }
        xSemaphoreGive(s_mu);
    }

    o += (size_t)snprintf(buf + o, n - o,
        "{\"ok\":true,\"scanning\":%s,\"adv\":%u,\"heap\":%u,\"heap_min\":%u,\"spock\":",
        scanning ? "true" : "false", (unsigned)adv,
        (unsigned)esp_get_free_heap_size(),
        (unsigned)esp_get_minimum_free_heap_size());
    if (o >= n) {
        buf[n - 1] = 0;
        return;
    }
    if (spock_used) {
        int w = write_dev(buf + o, n - o, &spock, now);
        if (w < 0 || (size_t)w >= n - o) {
            buf[n - 1] = 0;
            return;
        }
        o += (size_t)w;
    } else {
        o += (size_t)snprintf(buf + o, n - o,
                              "{\"mac\":\"00:33:7A:DF:F3:E1\",\"seen\":false}");
    }
    if (o >= n) {
        buf[n - 1] = 0;
        return;
    }
    o += (size_t)snprintf(buf + o, n - o, ",\"count\":%d,\"devices\":[", count);
    int first = 1;
    for (int i = 0; i < DEV_MAX && o < n; i++) {
        if (!devs[i].used) {
            continue;
        }
        if (!first) {
            if (o + 1 < n) {
                buf[o++] = ',';
                buf[o] = 0;
            }
        }
        first = 0;
        int w = write_dev(buf + o, n - o, &devs[i], now);
        if (w < 0 || (size_t)w >= n - o) {
            break;
        }
        o += (size_t)w;
    }
    if (o + 3 < n) {
        memcpy(buf + o, "]}", 3);
    } else if (n) {
        buf[n - 1] = 0;
    }
}
