#include "cyd_splash.h"
#include "cyd_display.h"

#include "sdkconfig.h"

#if CONFIG_APP_ENABLE_CYD

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_commands.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"

#include "cyd_scene.h"
#include "cyd_camera.h"
#include "nvs.h"
#include "cyd_cfg.h"
#include "driver/ledc.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "ha_client.h"
#include "wifi_manager.h"
#include "cyd_bmp.h"
#include "soc/soc_caps.h"

static const char *TAG = "cyd";

/* ESP32-2432S028 (Cheap Yellow Display, 2.8"). Same pins as TFT_eSPI's CYD setup. */
#define PIN_LCD_SCLK 14
#define PIN_LCD_MOSI 13
#define PIN_LCD_MISO 12 /* ILI9341 SDO; input, so it does not change the GPIO12 strap at reset */
#define PIN_LCD_CS   15
#define PIN_LCD_DC   2
#define PIN_LCD_BL   21
#define PIN_TOUCH_SCLK 25
#define PIN_TOUCH_MOSI 32
#define PIN_TOUCH_MISO 39
#define PIN_TOUCH_CS   33
#define PIN_TOUCH_IRQ  36

#define LCD_HOST   SPI2_HOST
#define TOUCH_HOST SPI3_HOST
#define BAND_H     20
#define SAVER_MS   ((int64_t)cyd_cfg()->saver_s * 1000LL)
#define SLEEP_MS   (40LL * 60LL * 1000LL)

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_lcd_io;
static spi_device_handle_t s_touch;
static int s_sntp_started;
static int s_screen_on = 1;

/* Camera viewer request (set from the HTTP task, consumed by the display task). */
static portMUX_TYPE s_cam_mux = portMUX_INITIALIZER_UNLOCKED;
static char s_cam_entity[64];
static int s_cam_secs;
static volatile int64_t s_test_until;

static int s_bl_ready;
static void bl_set(int on)
{
    if (!s_bl_ready) {
        ledc_timer_config_t t = { .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_8_BIT,
                                  .timer_num = LEDC_TIMER_1, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK };
        ledc_channel_config_t ch = { .gpio_num = PIN_LCD_BL, .speed_mode = LEDC_LOW_SPEED_MODE,
                                     .channel = LEDC_CHANNEL_1, .timer_sel = LEDC_TIMER_1, .duty = 0 };
        if (ledc_timer_config(&t) == ESP_OK && ledc_channel_config(&ch) == ESP_OK) s_bl_ready = 1;
    }
    if (!s_bl_ready) { gpio_set_level(PIN_LCD_BL, on ? 1 : 0); return; }
    int pct = cyd_cfg()->bright;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, on ? (uint32_t)(pct * 255 / 100) : 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

/* ---- Alerts (pushed by HA via POST /alert) ---- */
#define ALERT_MAX 8
typedef struct { char id[32]; char title[32]; char msg[48]; int level; int64_t expires; int64_t created; } alert_t;
static alert_t s_alerts[ALERT_MAX];
static portMUX_TYPE s_al_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile int s_alert_wake;

/* Panel font is ASCII: fold common pt-BR accents. */
static void ascii_fold(const char *in, char *out, size_t n)
{
    size_t o = 0;
    const unsigned char *p = (const unsigned char *)in;
    while (*p && o + 1 < n) {
        if (*p < 0x80) { out[o++] = (*p >= 0x20) ? (char)*p : ' '; p++; continue; }
        if ((*p == 0xC3) && p[1]) {
            unsigned char c = p[1]; char r = '?';
            if (c >= 0x80 && c <= 0x85) r = 'A'; else if (c == 0x87) r = 'C'; else if (c >= 0x88 && c <= 0x8B) r = 'E';
            else if (c >= 0x8C && c <= 0x8F) r = 'I'; else if (c >= 0x92 && c <= 0x96) r = 'O'; else if (c >= 0x99 && c <= 0x9C) r = 'U';
            else if (c >= 0xA0 && c <= 0xA5) r = 'a'; else if (c == 0xA7) r = 'c'; else if (c >= 0xA8 && c <= 0xAB) r = 'e';
            else if (c >= 0xAC && c <= 0xAF) r = 'i'; else if (c >= 0xB2 && c <= 0xB6) r = 'o'; else if (c >= 0xB9 && c <= 0xBC) r = 'u';
            out[o++] = r; p += 2; continue;
        }
        p++;
        while ((*p & 0xC0) == 0x80) p++;   /* skip other multibyte chars */
    }
    out[o] = '\0';
}

void cyd_alert_push(const char *id, const char *title, const char *msg, int level, int secs)
{
    alert_t a = {0};
    snprintf(a.id, sizeof a.id, "%s", (id && id[0]) ? id : "");
    ascii_fold(title ? title : "", a.title, sizeof a.title);
    ascii_fold(msg ? msg : "", a.msg, sizeof a.msg);
    a.level = level < 0 ? 0 : (level > 2 ? 2 : level);
    a.created = esp_timer_get_time();
    a.expires = secs > 0 ? a.created + (int64_t)secs * 1000000 : 0;
    if (!a.id[0]) snprintf(a.id, sizeof a.id, "a%lld", (long long)(a.created / 1000));
    portENTER_CRITICAL(&s_al_mux);
    int slot = -1, oldest = 0;
    for (int i = 0; i < ALERT_MAX; i++) {
        if (s_alerts[i].created && strcmp(s_alerts[i].id, a.id) == 0) { slot = i; break; }
        if (!s_alerts[i].created && slot < 0) slot = i;
        if (s_alerts[i].created < s_alerts[oldest].created) oldest = i;
    }
    if (slot < 0) slot = oldest;
    s_alerts[slot] = a;
    portEXIT_CRITICAL(&s_al_mux);
    s_alert_wake = 1;
}

void cyd_alert_dismiss(const char *id)
{
    portENTER_CRITICAL(&s_al_mux);
    for (int i = 0; i < ALERT_MAX; i++) {
        if (!id || !id[0] || strcmp(s_alerts[i].id, id) == 0) memset(&s_alerts[i], 0, sizeof s_alerts[i]);
    }
    portEXIT_CRITICAL(&s_al_mux);
}

void cyd_alerts_json(char *buf, size_t n)
{
    alert_t cp[ALERT_MAX];
    portENTER_CRITICAL(&s_al_mux);
    memcpy(cp, s_alerts, sizeof cp);
    portEXIT_CRITICAL(&s_al_mux);
    int k = snprintf(buf, n, "[");
    int first = 1;
    for (int i = 0; i < ALERT_MAX && k < (int)n - 200; i++) {
        if (!cp[i].created) continue;
        k += snprintf(buf + k, n - k, "%s{\"id\":\"%s\",\"title\":\"%s\",\"message\":\"%s\",\"level\":%d}",
                      first ? "" : ",", cp[i].id, cp[i].title, cp[i].msg, cp[i].level);
        first = 0;
    }
    snprintf(buf + k, n - k, "]");
}

/* Newest first into the scene; drops expired alerts. Returns ids for taps. */
static int fill_alerts(cyd_scene_t *sc, char ids[CYD_ALERT_ROWS][32])
{
    int64_t now = esp_timer_get_time();
    alert_t cp[ALERT_MAX];
    portENTER_CRITICAL(&s_al_mux);
    for (int i = 0; i < ALERT_MAX; i++) {
        if (s_alerts[i].created && s_alerts[i].expires && now > s_alerts[i].expires) memset(&s_alerts[i], 0, sizeof s_alerts[i]);
    }
    memcpy(cp, s_alerts, sizeof cp);
    portEXIT_CRITICAL(&s_al_mux);
    int count = 0;
    for (int r = 0; r < CYD_ALERT_ROWS; r++) {
        int best = -1;
        for (int i = 0; i < ALERT_MAX; i++) if (cp[i].created && (best < 0 || cp[i].created > cp[best].created)) best = i;
        if (best < 0) break;
        snprintf(sc->alerts[r].title, sizeof sc->alerts[r].title, "%s", cp[best].title);
        snprintf(sc->alerts[r].msg, sizeof sc->alerts[r].msg, "%s", cp[best].msg);
        sc->alerts[r].level = cp[best].level;
        if (ids) snprintf(ids[r], 32, "%s", cp[best].id);
        if (r == 0) sc->banner = (now - cp[best].created) < 15000000LL;
        cp[best].created = 0;
        count++;
    }
    for (int i = 0; i < ALERT_MAX; i++) if (cp[i].created) count++;
    sc->alert_rows = count < CYD_ALERT_ROWS ? count : CYD_ALERT_ROWS;
    return count;
}


/* DRAM, not the heap. On ESP-IDF 6, MALLOC_CAP_INTERNAL can return 32-bit
 * IRAM; a uint16 store there is a LoadStoreError and reboots the chip. */
static uint16_t s_band[CYD_W * BAND_H];
static uint16_t s_wire[CYD_W * BAND_H];

static int xpt_sample(uint8_t cmd)
{
    if (!s_touch) {
        return -1;
    }
    spi_transaction_t t = {
        .length = 24,
        .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
    };
    t.tx_data[0] = cmd;
    if (spi_device_transmit(s_touch, &t) != ESP_OK) {
        return -1;
    }
    return ((t.rx_data[1] << 8) | t.rx_data[2]) >> 3;
}

static int touch_down(void)
{
    if (!s_touch) {
        return gpio_get_level(PIN_TOUCH_IRQ) == 0;
    }
    /* 0xB1 reads Z1. 0x80 powers the ADC down afterwards. Without that
     * trailing command the XPT2046 leaves PENIRQ disabled, so the first
     * sample is the last one the saver ever sees. */
    int z = xpt_sample(0xB1);
    xpt_sample(0x80);
    if (z < 0) {
        return gpio_get_level(PIN_TOUCH_IRQ) == 0;
    }
    return z > 200 && z < 4000;
}

/* Touch position in screen coordinates (landscape). Uses the usual CYD
 * calibration (raw X 200..3700 -> 0..319, raw Y 240..3800 -> 0..239). */
static volatile int s_traw_x, s_traw_y, s_tx = -1, s_ty = -1, s_tcount;
/* Display-task heartbeat: a watchdog task reboots the board if it stalls. */
static volatile int64_t s_disp_beat;
#define DISP_STALL_US (45LL * 1000000LL)

void cyd_touch_json(char *buf, size_t n)
{
    int64_t now = esp_timer_get_time();
    snprintf(buf, n, "{\"raw_x\":%d,\"raw_y\":%d,\"x\":%d,\"y\":%d,\"count\":%d,"
             "\"uptime_s\":%lld,\"disp_age_ms\":%lld,\"reset\":%d,\"heap\":%u,\"heap_min\":%u}",
             s_traw_x, s_traw_y, s_tx, s_ty, s_tcount, (long long)(now / 1000000),
             (long long)(s_disp_beat ? (now - s_disp_beat) / 1000 : -1), (int)esp_reset_reason(),
             (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size());
}

static void disp_wdt_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        int64_t beat = s_disp_beat;
        if (beat && esp_timer_get_time() - beat > DISP_STALL_US) {
            ESP_LOGE(TAG, "display task stalled %lld ms, rebooting", (long long)((esp_timer_get_time() - beat) / 1000));
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_restart();
        }
    }
}

static int touch_xy(int *sx, int *sy)
{
    if (!s_touch || !touch_down()) {
        return 0;
    }
    int rx = 0, ry = 0, n = 0;
    for (int i = 0; i < 4; i++) {
        int x = xpt_sample(0x91);
        int y = xpt_sample(0xD1);
        if (x > 0 && y > 0) { rx += x; ry += y; n++; }
    }
    xpt_sample(0x80);
    if (!n) {
        return 0;
    }
    rx /= n; ry /= n;
    s_traw_x = rx; s_traw_y = ry;
    int x = (rx - 200) * CYD_W / 3500;
    int y = (ry - 240) * CYD_H / 3560;
    *sx = x < 0 ? 0 : (x >= CYD_W ? CYD_W - 1 : x);
    *sy = y < 0 ? 0 : (y >= CYD_H ? CYD_H - 1 : y);
    s_tx = *sx; s_ty = *sy; s_tcount++;
    return 1;
}

static void flush_band(const uint16_t *src, int y, int rows)
{
    for (int i = 0; i < CYD_W * rows; i++) {
        uint16_t c = src[i];
        s_wire[i] = (uint16_t)((c << 8) | (c >> 8));
    }
    esp_lcd_panel_draw_bitmap(s_panel, 0, y, CYD_W, y + rows, s_wire);
}

static void present(const cyd_scene_t *scene)
{
    for (int y = 0; y < CYD_H; y += BAND_H) {
        cyd_scene_paint(s_band, y, BAND_H, scene);
        flush_band(s_band, y, BAND_H);
    }
}

static void fill_time(cyd_scene_t *scene)
{
    time_t now = 0;
    struct tm tm;
    time(&now);
    localtime_r(&now, &tm);
    scene->time_valid = (tm.tm_year + 1900) >= 2024;
    scene->hour = tm.tm_hour;
    scene->minute = tm.tm_min;
    scene->wday = tm.tm_wday;
    scene->mday = tm.tm_mday;
    scene->month = tm.tm_mon;
}

static void set_screen(int on)
{
    if (on == s_screen_on) {
        return;
    }
    s_screen_on = on;
    if (s_panel) {
        esp_lcd_panel_disp_on_off(s_panel, on);
    }
    bl_set(on);
    ESP_LOGI(TAG, "panel %s", on ? "on" : "sleep");
}

static void fill_from_ha(cyd_scene_t *scene)
{
    ha_snapshot_t ha;
    ha_client_copy(&ha);
    snprintf(scene->place, sizeof scene->place, "%s", ha.place[0] ? ha.place : "Casa");
    snprintf(scene->status, sizeof scene->status, "%s", ha.status);
    if (ha.has_temp) {
        scene->temp_c = ha.temp_c;
        scene->has_temp = 1;
    }
    if (ha.has_humidity) {
        scene->humidity = ha.humidity;
        scene->has_humidity = 1;
    }
    scene->row_count = ha.device_count;
    if (scene->row_count > CYD_ROW_MAX) {
        scene->row_count = CYD_ROW_MAX;
    }
    for (int i = 0; i < scene->row_count; i++) {
        snprintf(scene->rows[i].name, sizeof scene->rows[i].name, "%s", ha.devices[i].name);
        snprintf(scene->rows[i].detail, sizeof scene->rows[i].detail, "%s", ha.devices[i].detail);
        scene->rows[i].on = ha.devices[i].on;
    }
}

static void maybe_sntp(void)
{
    if (s_sntp_started || !wifi_manager_is_connected()) {
        return;
    }
    setenv("TZ", "<-03>3", 1);
    tzset();
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    if (esp_netif_sntp_init(&cfg) == ESP_OK) {
        s_sntp_started = 1;
        ESP_LOGI(TAG, "SNTP started (America/Sao_Paulo, UTC-3)");
    }
}

void cyd_lcd_drain(void)
{
    /* tx_param waits for every queued color transfer, then polls a command.
     * ILI9341 NOP (0x00) does not change the panel. */
    if (s_lcd_io) {
        esp_lcd_panel_io_tx_param(s_lcd_io, 0x00, NULL, 0);
    }
}

static esp_err_t panel_bringup(void)
{
    gpio_config_t bl = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_LCD_BL,
    };
    esp_err_t err = gpio_config(&bl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "backlight gpio failed: %s", esp_err_to_name(err));
        return err;
    }
    gpio_set_level(PIN_LCD_BL, 0);

    spi_bus_config_t bus = {
        .sclk_io_num = PIN_LCD_SCLK,
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = PIN_LCD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CYD_W * BAND_H * (int)sizeof(uint16_t),
    };
    /* DMA on SPI2 fights the Wi-Fi driver on the ESP32 and reboots the CYD. */
    err = spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lcd spi bus failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        /* One clock for writes and reads. ILI9341 RAMRD is not reliable
         * above about 10 MHz, and 10 MHz divides the 80 MHz APB exactly. */
        .pclk_hz = 10 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = CYD_LCD_TRANS_QUEUE,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lcd panel io failed: %s", esp_err_to_name(err));
        return err;
    }
    s_lcd_io = io;

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
        .rgb_ele_order = cyd_cfg()->bgr ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB,
#else
        .rgb_endian = cyd_cfg()->bgr ? LCD_RGB_ENDIAN_BGR : LCD_RGB_ENDIAN_RGB,
#endif
        .bits_per_pixel = 16,
    };
    err = esp_lcd_new_panel_ili9341(io, &panel_cfg, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ili9341 create failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_lcd_panel_reset(s_panel);
    if (err == ESP_OK) {
        err = esp_lcd_panel_init(s_panel);
    }
    if (err == ESP_OK) {
        /* Landscape, same MADCTL as TFT_eSPI rotation 1 on this board. */
        err = esp_lcd_panel_swap_xy(s_panel, true);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_mirror(s_panel, false, false);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_invert_color(s_panel, cyd_cfg()->inv != 0);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_disp_on_off(s_panel, true);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ili9341 init failed: %s", esp_err_to_name(err));
        return err;
    }

    spi_bus_config_t tbus = {
        .sclk_io_num = PIN_TOUCH_SCLK,
        .mosi_io_num = PIN_TOUCH_MOSI,
        .miso_io_num = PIN_TOUCH_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };
    err = spi_bus_initialize(TOUCH_HOST, &tbus, SPI_DMA_DISABLED);
    if (err == ESP_OK) {
        spi_device_interface_config_t dev = {
            .clock_speed_hz = 2500000,
            .mode = 0,
            .spics_io_num = PIN_TOUCH_CS,
            .queue_size = 1,
        };
        err = spi_bus_add_device(TOUCH_HOST, &dev, &s_touch);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "touch bus unavailable (%s); screen still runs", esp_err_to_name(err));
        s_touch = NULL;
    }

    gpio_config_t irq = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = 1ULL << PIN_TOUCH_IRQ,
    };
    gpio_config(&irq);

    bl_set(1);
    ESP_LOGI(TAG, "CYD panel ready (320x240)");
    return ESP_OK;
}

static void shot_service(void);

static void wait_release(int max_ms)
{
    for (int t = 0; t < max_ms && touch_down(); t += 30) {
        shot_service();
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

static void fill_black(void)
{
    memset(s_wire, 0, sizeof s_wire);
    for (int y = 0; y < CYD_H; y += BAND_H) {
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, CYD_W, y + BAND_H, s_wire);
    }
}

/* Live snapshots of `entity`, ~1/s, until `secs` pass or the screen is tapped. */
static void run_camera(const char *entity, int secs)
{
    ESP_LOGI(TAG, "camera %s for %ds", entity, secs);
    set_screen(1);
    shot_service();
    wait_release(1500);
    fill_black();
    int64_t until = esp_timer_get_time() + (int64_t)secs * 1000000;
    int fails = 0;
    while (esp_timer_get_time() < until) {
        int64_t t0 = esp_timer_get_time();
        s_disp_beat = t0;
        shot_service();
        uint8_t *jpg = NULL;
        int len = 0;
        if (ha_client_fetch_camera(entity, &jpg, &len) == ESP_OK &&
            cyd_camera_draw(s_panel, jpg, len) == ESP_OK) {
            fails = 0;
        } else if (++fails >= 4) {
            free(jpg);
            break;
        }
        free(jpg);
        /* ~1 frame per second; a new tap closes the viewer. */
        while ((esp_timer_get_time() - t0) < (int64_t)cyd_cfg()->cam_ms * 1000) {
            shot_service();
            if (touch_down()) {
                wait_release(1500);
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(40));
        }
        portENTER_CRITICAL(&s_cam_mux);
        if (s_cam_secs > 0) {   /* a new request while open: extend/switch */
            until = esp_timer_get_time() + (int64_t)s_cam_secs * 1000000;
            s_cam_secs = 0;
        }
        portEXIT_CRITICAL(&s_cam_mux);
    }
}

void cyd_display_show_camera(const char *entity, int seconds)
{
    if (!entity || !entity[0]) return;
    if (seconds <= 0 || seconds > 120) seconds = 20;
    portENTER_CRITICAL(&s_cam_mux);
    snprintf(s_cam_entity, sizeof s_cam_entity, "%s", entity);
    s_cam_secs = seconds;
    portEXIT_CRITICAL(&s_cam_mux);
}

/* The panel (SPI) and backlight (LEDC) belong to the display task. Other
 * tasks (HTTP server) only raise these flags; display_task applies them.
 * Touching the panel from the HTTP task while a frame was being pushed
 * deadlocked both tasks (CYD froze after POST /config). */
static volatile int s_cfg_pending;
static volatile int s_inv_pending = -1;

void cyd_display_apply_cfg(void)
{
    s_cfg_pending = 1;
}

void cyd_display_set_color(int invert, int bgr, int test)
{
    if (invert >= 0) cyd_cfg_set_u8("disp_inv", invert ? 1 : 0);
    if (bgr >= 0) cyd_cfg_set_u8("disp_bgr", bgr ? 1 : 0);
    if (invert >= 0) s_inv_pending = invert ? 1 : 0;
    if (test) s_test_until = esp_timer_get_time() + 15000000LL;
}

/* Display task only. */
static void apply_pending(void)
{
    int inv = s_inv_pending;
    if (inv >= 0) {
        s_inv_pending = -1;
        if (s_panel) esp_lcd_panel_invert_color(s_panel, inv != 0);
    }
    if (s_cfg_pending) {
        s_cfg_pending = 0;
        if (s_panel) esp_lcd_panel_invert_color(s_panel, cyd_cfg()->inv != 0);
        if (s_screen_on) bl_set(1);
    }
}

/* Screenshot handshake. HTTP only flips the state to REQ; this task is the
 * only one that touches the panel. Bands are 4 RGB565 rows (2560 bytes),
 * bottom row first, passed through a queue. */
#define CYD_SHOT_ROWS 4
_Static_assert(CYD_W * CYD_SHOT_ROWS * 2 >= 2048, "shared shot buffer under 2KB");
_Static_assert(CYD_W * CYD_SHOT_ROWS * 2 <= 4096, "shared shot buffer over 4KB");

enum { SHOT_IDLE = 0, SHOT_REQ = 1, SHOT_RUN = 2, SHOT_CANCEL = 3 };

static atomic_int s_shot_st;
static atomic_int s_in_shot;
static QueueHandle_t s_shot_q;
static QueueHandle_t s_shot_ackq;
static uint16_t s_shot[CYD_W * CYD_SHOT_ROWS];
_Static_assert(CYD_LCD_READ_FIFO <= SOC_SPI_MAXIMUM_BUFFER_SIZE,
               "LCD read chunk exceeds the non-DMA SPI FIFO");
_Static_assert(1 + CYD_LCD_READ_PX(CYD_LCD_READ_FIFO) * 3 <= CYD_LCD_READ_FIFO,
               "RGB666 chunk does not fit in one SPI transaction");
/* Full-frame RGB565 sprite, row-major. The stock UI never installs one
 * (each band is painted and discarded), so capture reads the panel. */
static const uint16_t *s_sprite;

static int shot_waiting(void)
{
    return atomic_load(&s_shot_st) == SHOT_REQ;
}

static int shot_ensure(void)
{
    if (s_shot_q && s_shot_ackq) {
        return 1;
    }
    if (!s_shot_q) {
        s_shot_q = xQueueCreate(1, sizeof(int));
    }
    if (!s_shot_ackq) {
        s_shot_ackq = xQueueCreate(1, sizeof(uint8_t));
    }
    return s_shot_q && s_shot_ackq;
}

static void shot_drain(void)
{
    int dump;
    uint8_t ack;
    while (s_shot_q && xQueueReceive(s_shot_q, &dump, 0) == pdTRUE) {
    }
    while (s_shot_ackq && xQueueReceive(s_shot_ackq, &ack, 0) == pdTRUE) {
    }
}

static void store_px(uint16_t *dst, uint16_t c)
{
    if (cyd_cfg()->inv) {
        c ^= 0xFFFF; /* INVON flips the glass, not the GRAM */
    }
    *dst = c;
}

/* One RAMRD or RAMRD-continue burst. Both commands clock a dummy byte and
 * then RGB666 (top 6 bits of R, G, B). `px` must fit in the non-DMA FIFO. */
static esp_err_t panel_read_chunk(int cmd, uint16_t *dst, int px)
{
    uint8_t raw[CYD_LCD_READ_FIFO];
    int nbytes = 1 + px * 3;
    if (px <= 0 || nbytes > (int)sizeof raw) {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t err = esp_lcd_panel_io_rx_param(s_lcd_io, cmd, raw, nbytes);
    if (err != ESP_OK) {
        return err;
    }
    const uint8_t *p = raw + 1;
    for (int i = 0; i < px; i++, p += 3) {
        uint16_t c = (uint16_t)(((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3));
        store_px(&dst[i], c);
    }
    return ESP_OK;
}

static esp_err_t panel_read_line(uint16_t *dst, int y)
{
    uint8_t caset[4] = {
        0, 0, (uint8_t)((CYD_W - 1) >> 8), (uint8_t)(CYD_W - 1),
    };
    uint8_t raset[4] = {
        (uint8_t)(y >> 8), (uint8_t)y, (uint8_t)(y >> 8), (uint8_t)y,
    };
    esp_err_t err = esp_lcd_panel_io_tx_param(s_lcd_io, LCD_CMD_CASET, caset, 4);
    if (err == ESP_OK) {
        err = esp_lcd_panel_io_tx_param(s_lcd_io, LCD_CMD_RASET, raset, 4);
    }
    if (err != ESP_OK) {
        return err;
    }
    /* SPI_DMA_DISABLED caps one transfer at the 64-byte FIFO. 21 px is
     * 1 dummy + 63 colour bytes. RAMRD starts the line; 0x3E continues it
     * after CS is released between transactions. */
    const int px_chunk = cyd_lcd_read_px(CYD_LCD_READ_FIFO);
    int x = 0;
    int cmd = LCD_CMD_RAMRD;
    while (x < CYD_W) {
        s_disp_beat = esp_timer_get_time();
        int n = CYD_W - x;
        if (n > px_chunk) {
            n = px_chunk;
        }
        err = panel_read_chunk(cmd, dst + x, n);
        if (err != ESP_OK) {
            return err;
        }
        x += n;
        cmd = LCD_CMD_RAMRDC;
    }
    return ESP_OK;
}

/* One screen line into `dst`. Sprite/framebuffer when one is installed,
 * otherwise the ILI9341 GRAM. Display task only. */
static esp_err_t read_line(uint16_t *dst, int y)
{
    s_disp_beat = esp_timer_get_time();
    if (s_sprite) {
        const uint16_t *src = s_sprite + (size_t)y * CYD_W;
        for (int x = 0; x < CYD_W; x++) {
            store_px(&dst[x], src[x]);
        }
        return ESP_OK;
    }
    if (!s_lcd_io) {
        return ESP_ERR_INVALID_STATE;
    }
    return panel_read_line(dst, y);
}

/* `rows` of screen y, stored bottom-up so the HTTP task can stream a BMP. */
static esp_err_t read_band(uint16_t *dst, int y, int rows)
{
    for (int i = 0; i < rows; i++) {
        if (atomic_load(&s_shot_st) != SHOT_RUN) {
            return ESP_ERR_INVALID_STATE;
        }
        esp_err_t err = read_line(dst + i * CYD_W, y + rows - 1 - i);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

static int shot_send(int rows)
{
    int64_t t0 = esp_timer_get_time();
    for (;;) {
        s_disp_beat = esp_timer_get_time();
        if (atomic_load(&s_shot_st) != SHOT_RUN) {
            return 0;
        }
        if (xQueueSend(s_shot_q, &rows, pdMS_TO_TICKS(200)) == pdTRUE) {
            return 1;
        }
        if (esp_timer_get_time() - t0 > (int64_t)CYD_SHOT_TIMEOUT_MS * 1000) {
            return 0;
        }
    }
}

static int shot_wait_ack(void)
{
    int64_t t0 = esp_timer_get_time();
    for (;;) {
        s_disp_beat = esp_timer_get_time();
        if (atomic_load(&s_shot_st) != SHOT_RUN) {
            return 0;
        }
        uint8_t ack;
        if (xQueueReceive(s_shot_ackq, &ack, pdMS_TO_TICKS(200)) == pdTRUE) {
            return atomic_load(&s_shot_st) == SHOT_RUN;
        }
        if (esp_timer_get_time() - t0 > (int64_t)CYD_SHOT_TIMEOUT_MS * 1000) {
            return 0;
        }
    }
}

static void shot_service(void)
{
    int expected = SHOT_REQ;
    if (!atomic_compare_exchange_strong(&s_shot_st, &expected, SHOT_RUN)) {
        return;
    }
    atomic_store(&s_in_shot, 1);
    s_disp_beat = esp_timer_get_time();
    apply_pending();
    cyd_lcd_drain();

    int fail_msg = 0;
    for (int y = CYD_H; y > 0; ) {
        s_disp_beat = esp_timer_get_time();
        int rows = CYD_SHOT_ROWS;
        if (rows > y) {
            rows = y;
        }
        int y0 = y - rows;
        esp_err_t err = read_band(s_shot, y0, rows);
        if (err != ESP_OK) {
            if (err != ESP_ERR_INVALID_STATE) {
                ESP_LOGE(TAG, "screenshot read failed: %s (0x%x)", esp_err_to_name(err), (unsigned)err);
                fail_msg = -2;
            } else {
                fail_msg = -1;
            }
            break;
        }
        if (!shot_send(rows) || !shot_wait_ack()) {
            fail_msg = -1;
            break;
        }
        y = y0;
    }
    if (!fail_msg) {
        int done = 0;
        shot_send(done);
    } else {
        xQueueSend(s_shot_q, &fail_msg, 0);
    }
    atomic_store(&s_shot_st, SHOT_IDLE);
    atomic_store(&s_in_shot, 0);
}

esp_err_t cyd_display_shot_begin(void)
{
    if (!shot_ensure()) {
        return ESP_ERR_NO_MEM;
    }
    /* Drop a stale band before publishing REQ, so display_task cannot
     * enqueue the new frame until the queue is empty. */
    shot_drain();
    int idle = SHOT_IDLE;
    if (!atomic_compare_exchange_strong(&s_shot_st, &idle, SHOT_REQ)) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

int cyd_display_shot_recv(const uint16_t **rgb565, int *rows, int timeout_ms)
{
    int msg = 0;
    if (xQueueReceive(s_shot_q, &msg, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return -1;
    }
    if (msg == -2) {
        return -2;
    }
    if (msg < 0) {
        return -1;
    }
    if (msg == 0) {
        return 0;
    }
    *rgb565 = s_shot;
    *rows = msg;
    return 1;
}

void cyd_display_shot_ack(void)
{
    uint8_t ack = 1;
    xQueueSend(s_shot_ackq, &ack, pdMS_TO_TICKS(CYD_SHOT_TIMEOUT_MS));
}

void cyd_display_shot_end(void)
{
    int st = atomic_load(&s_shot_st);
    if (st == SHOT_IDLE) {
        return;
    }
    if (st == SHOT_REQ) {
        int expected = SHOT_REQ;
        if (atomic_compare_exchange_strong(&s_shot_st, &expected, SHOT_IDLE)) {
            return;
        }
    }
    atomic_store(&s_shot_st, SHOT_CANCEL);
    uint8_t ack = 1;
    int64_t t0 = esp_timer_get_time();
    while (atomic_load(&s_in_shot) && esp_timer_get_time() - t0 < 1000000) {
        xQueueSend(s_shot_ackq, &ack, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void display_task(void *arg)
{
    (void)arg;
    cyd_splash_play(s_panel, CYD_W, CYD_H, 2);
    int64_t idle_since = esp_timer_get_time();
    int frame = 0;
    int last_mode = -1;
    int behavior = 0, beh_start = 0, beh_len = 24;
    int was_down = 0;
    int alerts_view = 0, long_done = 0, alert_count_last = 0, banner_last = 0;
    int64_t down_since = 0;
    int64_t alerts_since = 0;
    char alert_ids[CYD_ALERT_ROWS][32];
    memset(alert_ids, 0, sizeof alert_ids);

    while (1) {
        s_disp_beat = esp_timer_get_time();
        maybe_sntp();
        apply_pending();
        shot_service();

        /* Camera requested by HA (doorbell) or by the quick button. */
        char cam[64] = {0};
        int cam_secs = 0;
        portENTER_CRITICAL(&s_cam_mux);
        if (s_cam_secs > 0) {
            memcpy(cam, s_cam_entity, sizeof cam);
            cam_secs = s_cam_secs;
            s_cam_secs = 0;
        }
        portEXIT_CRITICAL(&s_cam_mux);
        if (cam_secs > 0) {
            run_camera(cam, cam_secs);
            idle_since = esp_timer_get_time();
            last_mode = -1;
            was_down = 0;
        }

        if (s_alert_wake) {
            s_alert_wake = 0;
            if (cyd_cfg()->alerts) {
                idle_since = esp_timer_get_time();
                if (!s_screen_on) set_screen(1);
            }
        }

        int tx = -1, ty = -1;
        int down = touch_xy(&tx, &ty) || touch_down();
        if (down) {
            int on_saver = last_mode == CYD_UI_SAVER;
            int woke = !s_screen_on || (last_mode != CYD_UI_HOME && last_mode != CYD_UI_ALERTS);
            idle_since = esp_timer_get_time();
            if (!s_screen_on) {
                set_screen(1);
            }
            if (!was_down) down_since = esp_timer_get_time();
            int has_alerts = alert_count_last > 0;
            /* long press (0.8 s) anywhere while alerts exist: clear them all */
            if (was_down && has_alerts && !long_done && esp_timer_get_time() - down_since > 800000) {
                cyd_alert_dismiss(NULL);
                alerts_view = 0;
                long_done = 1;
            }
            if (!was_down) long_done = 0;
            if (!was_down && alerts_view) {
                alerts_since = esp_timer_get_time();
                if (ty >= 180) cyd_alert_dismiss(NULL);   /* big "Limpar tudo" button */
                alerts_view = 0;                          /* any other tap: seen, close */
                was_down = 1;
                continue;
            }
            /* big target: whole top strip (left of the Porta button) or the banner */
            if (!was_down && has_alerts && (last_mode == CYD_UI_HOME || on_saver) && ty >= 0 &&
                ((ty < 70 && tx < CYD_CAMBTN_X - 6) || (banner_last && ty >= 190))) {
                alerts_view = 1;
                alerts_since = esp_timer_get_time();
                was_down = 1;
                continue;
            }
            if (!was_down && !woke && tx >= CYD_CAMBTN_X - 10 && ty >= 0 && ty <= CYD_CAMBTN_Y + CYD_CAMBTN_H + 10 &&
                wifi_manager_is_connected()) {
                cyd_display_show_camera(cyd_cfg()->cam_quick, cyd_cfg()->bell_secs);
                was_down = 1;
                continue;
            }
        }
        was_down = down;

        if (frame - beh_start >= beh_len) {
            int prev = behavior;
            do { behavior = (int)(esp_random() % 5); } while (behavior == prev);
            beh_start = frame;
            beh_len = behavior == 1 ? 48 + (int)(esp_random() % 32) : 20 + (int)(esp_random() % 16);
        }

        int64_t idle_ms = (esp_timer_get_time() - idle_since) / 1000;
        cyd_scene_t scene;
        memset(&scene, 0, sizeof scene);
        scene.tail = frame / 2;
        scene.behavior = behavior;
        scene.phase = frame - beh_start;
        scene.cam_btn = 1;
        scene.test_bars = esp_timer_get_time() < s_test_until;
        scene.blink = ((frame % 16) >= 14);
        fill_time(&scene);
        if (!cyd_cfg()->clk24 && scene.time_valid) {
            scene.hour = scene.hour % 12;
            if (scene.hour == 0) scene.hour = 12;
        }
        fill_from_ha(&scene);
        scene.cat_on = cyd_cfg()->cat;
        {
            ha_snapshot_t hs;
            ha_client_copy(&hs);
            scene.updates = hs.updates;
            if (!wifi_manager_is_connected()) scene.net_state = 0;
            else if (hs.internet == 1) scene.net_state = 2;
            else if (hs.internet == 0) scene.net_state = 1;
            else scene.net_state = 3;
        }
        if (cyd_cfg()->alerts) {
            int n = fill_alerts(&scene, alert_ids);
            scene.alert_count = n;   /* updates only listed, never block the badge */
        }
        alert_count_last = scene.alert_count;
        banner_last = scene.banner;
        if (!scene.has_temp && !scene.has_humidity && scene.row_count == 0 &&
            strcmp(scene.status, "HA ok") != 0) {
            /* Keep the glanceable numbers until HA answers. */
            scene.temp_c = 26;
            scene.humidity = 62;
            scene.has_temp = 0;
            scene.has_humidity = 0;
        }
        const char *ip = wifi_manager_get_ip();
        if (ip) {
            snprintf(scene.ip, sizeof scene.ip, "%s", ip);
        }

        if (!wifi_manager_is_connected()) {
            scene.mode = CYD_UI_PORTAL;
            if (!s_screen_on) {
                set_screen(1);
            }
        } else if (idle_ms >= SLEEP_MS) {
            scene.mode = CYD_UI_SLEEP;
            set_screen(0);
        } else if (idle_ms >= SAVER_MS) {
            scene.mode = CYD_UI_SAVER;
            scene.blink = 1;
            if (!scene.has_temp) {
                scene.temp_c = 26;
            }
            if (!scene.has_humidity) {
                scene.humidity = 62;
            }
            if (!s_screen_on) {
                set_screen(1);
            }
        } else {
            scene.mode = CYD_UI_HOME;
            if (!s_screen_on) {
                set_screen(1);
            }
        }
        if (alerts_view && (esp_timer_get_time() - alerts_since) > 30000000LL) alerts_view = 0;
        if (alerts_view && wifi_manager_is_connected()) {
            scene.mode = CYD_UI_ALERTS;
            idle_since = esp_timer_get_time();
        }

        if (scene.mode == CYD_UI_SLEEP) {
            if (last_mode != CYD_UI_SLEEP) {
                present(&scene);
                last_mode = CYD_UI_SLEEP;
            }
        } else {
            present(&scene);
            last_mode = scene.mode;
        }
        frame++;

        int samples = (scene.mode == CYD_UI_SLEEP) ? 10 : 5;
        for (int i = 0; i < samples; i++) {
            if (shot_waiting()) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            if (s_cam_secs > 0) {
                break;
            }
            if (touch_down()) {
                if (!was_down) {
                    break;   /* handle the new tap (wake / button) right away */
                }
                idle_since = esp_timer_get_time();
            }
        }
    }
}

esp_err_t cyd_display_start(void)
{
    esp_err_t err = panel_bringup();
    if (err != ESP_OK) {
        return err;
    }
    if (ha_client_start() != ESP_OK) {
        ESP_LOGW(TAG, "Home Assistant poller did not start");
    }
    s_disp_beat = esp_timer_get_time();
    xTaskCreate(disp_wdt_task, "cyd_wdt", 2560, NULL, 5, NULL);
    if (xTaskCreate(display_task, "cyd", 8192, NULL, 1, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

#else /* !CONFIG_APP_ENABLE_CYD */
#include <stdio.h>

void cyd_display_apply_cfg(void) {}
void cyd_touch_json(char *buf, size_t n) { if (n) snprintf(buf, n, "{}"); }
void cyd_alert_push(const char *id, const char *title, const char *msg, int level, int secs) { (void)id; (void)title; (void)msg; (void)level; (void)secs; }
void cyd_alert_dismiss(const char *id) { (void)id; }
void cyd_alerts_json(char *buf, size_t n) { if (n) snprintf(buf, n, "[]"); }


void cyd_display_show_camera(const char *entity, int seconds) { (void)entity; (void)seconds; }
void cyd_lcd_drain(void) {}
void cyd_display_set_color(int invert, int bgr, int test) { (void)invert; (void)bgr; (void)test; }
esp_err_t cyd_display_shot_begin(void) { return ESP_ERR_INVALID_STATE; }
int cyd_display_shot_recv(const uint16_t **rgb565, int *rows, int timeout_ms)
{
    (void)rgb565; (void)rows; (void)timeout_ms;
    return -1;
}
void cyd_display_shot_ack(void) {}
void cyd_display_shot_end(void) {}


esp_err_t cyd_display_start(void)
{
    return ESP_OK;
}

#endif
