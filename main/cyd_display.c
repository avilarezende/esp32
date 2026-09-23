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
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"

#include "cyd_scene.h"
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
#define IDLE_MS    30000

static esp_lcd_panel_handle_t s_panel;
static spi_device_handle_t s_touch;
static int s_sntp_started;

static int xpt_sample(uint8_t cmd)
{
    if (!s_touch) {
        return 0;
    }
    spi_transaction_t t = {
        .length = 24,
        .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
    };
    t.tx_data[0] = cmd;
    if (spi_device_transmit(s_touch, &t) != ESP_OK) {
        return 0;
    }
    return ((t.rx_data[1] << 8) | t.rx_data[2]) >> 3;
}

static int touch_down(void)
{
    if (gpio_get_level(PIN_TOUCH_IRQ) != 0) {
        return 0;
    }
    int z1 = xpt_sample(0xB1);
    int z2 = xpt_sample(0xC1);
    int pressure = z1 + 4095 - z2;
    return pressure > 400;
}

static void flush_band(const uint16_t *src, int y, int rows)
{
    static uint16_t *wire;
    if (!wire) {
        wire = heap_caps_malloc(CYD_W * BAND_H * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    }
    if (!wire) {
        return;
    }
    for (int i = 0; i < CYD_W * rows; i++) {
        uint16_t c = src[i];
        wire[i] = (uint16_t)((c << 8) | (c >> 8));
    }
    esp_lcd_panel_draw_bitmap(s_panel, 0, y, CYD_W, y + rows, wire);
}

static void present(const cyd_scene_t *scene)
{
    static uint16_t *band;
    if (!band) {
        band = heap_caps_malloc(CYD_W * BAND_H * sizeof(uint16_t), MALLOC_CAP_INTERNAL);
        if (!band) {
            return;
        }
    }
    for (int y = 0; y < CYD_H; y += BAND_H) {
        cyd_scene_paint(band, y, BAND_H, scene);
        flush_band(band, y, BAND_H);
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
    ESP_ERROR_CHECK(gpio_config(&bl));
    gpio_set_level(PIN_LCD_BL, 0);

    spi_bus_config_t bus = {
        .sclk_io_num = PIN_LCD_SCLK,
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CYD_W * BAND_H * (int)sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io));

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#else
        .rgb_endian = LCD_RGB_ENDIAN_BGR,
#endif
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io, &panel_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    /* Landscape, same MADCTL as TFT_eSPI rotation 1 on this board. */
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, false, false));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    spi_bus_config_t tbus = {
        .sclk_io_num = PIN_TOUCH_SCLK,
        .mosi_io_num = PIN_TOUCH_MOSI,
        .miso_io_num = PIN_TOUCH_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };
    esp_err_t err = spi_bus_initialize(TOUCH_HOST, &tbus, SPI_DMA_DISABLED);
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

static void display_task(void *arg)
{
    (void)arg;
    int64_t idle_since = esp_timer_get_time();
    int was_down = 0;
    int frame = 0;

    while (1) {
        maybe_sntp();
        int down = touch_down();
        if (down && !was_down) {
            idle_since = esp_timer_get_time();
        }
        was_down = down;

        cyd_scene_t scene;
        memset(&scene, 0, sizeof scene);
        scene.temp_c = 26;
        scene.humidity = 62;
        scene.tail = frame / 2;
        scene.blink = ((frame % 16) >= 14);
        fill_time(&scene);
        const char *ip = wifi_manager_get_ip();
        if (ip) {
            snprintf(scene.ip, sizeof scene.ip, "%s", ip);
        }

        int64_t idle_ms = (esp_timer_get_time() - idle_since) / 1000;
        if (wifi_manager_is_provisioning()) {
            scene.mode = CYD_UI_PORTAL;
        } else if (idle_ms >= IDLE_MS) {
            scene.mode = CYD_UI_SAVER;
            scene.blink = 1;
        } else {
            scene.mode = CYD_UI_AWAKE;
        }

        present(&scene);
        frame++;
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

esp_err_t cyd_display_start(void)
{
    esp_err_t err = panel_bringup();
    if (err != ESP_OK) {
        return err;
    }
    if (xTaskCreate(display_task, "cyd", 6144, NULL, 1, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

#else /* !CONFIG_APP_ENABLE_CYD */

esp_err_t cyd_display_start(void)
{
    return ESP_OK;
}

#endif
