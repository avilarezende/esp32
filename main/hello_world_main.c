/*
 * ESP32 starter firmware.
 *
 * Prints chip information on boot and then emits a heartbeat message once per
 * second. This runs on real ESP32 hardware and under the ESP-IDF QEMU
 * emulator (`idf.py qemu`), which makes it usable for CI and headless
 * development environments.
 */

#include <stdio.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"

void app_main(void)
{
    printf("Hello from the esp32 project!\n");

    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);

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

    for (int i = 0; ; i++) {
        printf("heartbeat #%d - firmware alive\n", i);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
