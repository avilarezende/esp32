#include "cyd_splash.h"
#include "cyd_display.h"

#include "sdkconfig.h"

#if CONFIG_APP_ENABLE_CYD

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"

#include "cyd_scene.h"
#include "cyd_camera.h"
#include "nvs.h"
#include "esp_random.h"
#include "ha_client.h"
#include "wifi_manager.h"

static const char *TAG = "cyd";

/* ESP32-2432S028 (Cheap Yellow Display, 2.8"). Same pins as TFT_eSPI's CYD setup. */
#define PIN_LCD_SCLK 14
#define PIN_LCD_MOSI 13
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
#define SAVER_MS   30000LL
#define SLEEP_MS   (40LL * 60LL * 1000LL)

static esp_lcd_panel_handle_t s_panel;
static spi_device_handle_t s_touch;
static int s_sntp_started;
static int s_screen_on = 1;

/* Camera viewer request (set from the HTTP task, consumed by the display task). */
static portMUX_TYPE s_cam_mux = portMUX_INITIALIZER_UNLOCKED;
static char s_cam_entity[64];
static int s_cam_secs;
static volatile int64_t s_test_until;

static uint8_t cfg_u8(const char *key, uint8_t def)
{
    nvs_handle_t h;
    uint8_t v = def;
    if (nvs_open("app_cfg", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, key, &v);
        nvs_close(h);
    }
    return v;
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
    int x = (rx - 200) * CYD_W / 3500;
    int y = (ry - 240) * CYD_H / 3560;
    *sx = x < 0 ? 0 : (x >= CYD_W ? CYD_W - 1 : x);
    *sy = y < 0 ? 0 : (y >= CYD_H ? CYD_H - 1 : y);
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
    gpio_set_level(PIN_LCD_BL, on ? 1 : 0);
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
        .miso_io_num = -1,
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
        .pclk_hz = 20 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lcd panel io failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
        .rgb_ele_order = cfg_u8("disp_bgr", 1) ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB,
#else
        .rgb_endian = cfg_u8("disp_bgr", 1) ? LCD_RGB_ENDIAN_BGR : LCD_RGB_ENDIAN_RGB,
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
        err = esp_lcd_panel_invert_color(s_panel, cfg_u8("disp_inv", 1) != 0);
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

    gpio_set_level(PIN_LCD_BL, 1);
    ESP_LOGI(TAG, "CYD panel ready (320x240)");
    return ESP_OK;
}

static void wait_release(int max_ms)
{
    for (int t = 0; t < max_ms && touch_down(); t += 30) {
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
    wait_release(1500);
    fill_black();
    int64_t until = esp_timer_get_time() + (int64_t)secs * 1000000;
    int fails = 0;
    while (esp_timer_get_time() < until) {
        int64_t t0 = esp_timer_get_time();
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
        while ((esp_timer_get_time() - t0) < 1000000) {
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

void cyd_display_set_color(int invert, int bgr, int test)
{
    nvs_handle_t h;
    if ((invert >= 0 || bgr >= 0) && nvs_open("app_cfg", NVS_READWRITE, &h) == ESP_OK) {
        if (invert >= 0) nvs_set_u8(h, "disp_inv", invert ? 1 : 0);
        if (bgr >= 0) nvs_set_u8(h, "disp_bgr", bgr ? 1 : 0);
        nvs_commit(h);
        nvs_close(h);
    }
    if (invert >= 0 && s_panel) esp_lcd_panel_invert_color(s_panel, invert != 0);
    if (test) s_test_until = esp_timer_get_time() + 15000000LL;
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

    while (1) {
        maybe_sntp();

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

        int tx = 0, ty = 0;
        int down = touch_xy(&tx, &ty) || touch_down();
        if (down) {
            int woke = !s_screen_on || last_mode != CYD_UI_HOME;
            idle_since = esp_timer_get_time();
            if (!s_screen_on) {
                set_screen(1);
            }
            if (!was_down && !woke && tx >= CYD_CAMBTN_X - 10 && ty <= CYD_CAMBTN_Y + CYD_CAMBTN_H + 10 &&
                wifi_manager_is_connected()) {
                cyd_display_show_camera("camera.frigate_frente_direita", 20);
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
        fill_from_ha(&scene);
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
    if (xTaskCreate(display_task, "cyd", 8192, NULL, 1, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

#else /* !CONFIG_APP_ENABLE_CYD */

void cyd_display_show_camera(const char *entity, int seconds) { (void)entity; (void)seconds; }
void cyd_display_set_color(int invert, int bgr, int test) { (void)invert; (void)bgr; (void)test; }


esp_err_t cyd_display_start(void)
{
    return ESP_OK;
}

#endif
