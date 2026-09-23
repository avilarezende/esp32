/*
 * ESP32 firmware entry point.
 *
 * On boot the device tries to join the Wi-Fi network stored in NVS. When no
 * credentials are stored (or the connection fails), it starts a SoftAP
 * configuration portal ("ESP32-Setup") that serves a web page for entering the
 * network to join. See wifi_manager.c / http_server.c.
 *
 * Runs on real ESP32 hardware and, for the configuration portal, under the
 * ESP-IDF QEMU emulator (reachable over the emulated Ethernet interface).
 */

#include <inttypes.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "cyd_display.h"
#include "wifi_manager.h"

static const char *TAG = "app";

static void print_chip_info(void)
{
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);

    printf("Hello from the esp32 project!\n");
    printf("Chip: %s with %d CPU core(s), features:%s%s%s%s, revision v%d.%d\n",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? " WiFi" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? " BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? " BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? " 802.15.4" : "",
           chip_info.revision / 100,
           chip_info.revision % 100);

    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        printf("Flash size: %" PRIu32 " MB\n", flash_size / (uint32_t)(1024 * 1024));
    }
    printf("Free heap: %" PRIu32 " bytes\n", esp_get_free_heap_size());
}

void app_main(void)
{
    print_chip_info();

    /* NVS is required by the Wi-Fi stack and stores the saved credentials. */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(wifi_manager_start());
    if (cyd_display_start() != ESP_OK) {
        ESP_LOGW(TAG, "CYD display did not start; web portal still available");
    }

    for (int i = 0; ; i++) {
        if (wifi_manager_is_connected()) {
            ESP_LOGI(TAG, "heartbeat #%d - Wi-Fi connected", i);
        } else if (wifi_manager_is_provisioning()) {
            ESP_LOGI(TAG, "heartbeat #%d - waiting for setup at '%s' portal", i, WIFI_MANAGER_AP_SSID);
        } else {
            ESP_LOGI(TAG, "heartbeat #%d - Wi-Fi not connected", i);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
