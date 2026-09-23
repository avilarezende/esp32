idf_component_register(SRCS "main.c"
                            "wifi_manager.c"
                            "http_server.c"
                            "qemu_eth.c"
                            "assistant.c"
                            "cyd_scene.c"
                            "cyd_display.c"
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES "app.html"
                       REQUIRES spi_flash
                                esp_wifi
                                esp_lcd
                                esp_lcd_ili9341
                                driver
                                esp_timer
                                esp_http_server
                                esp_netif
                                esp_event
                                nvs_flash
                                esp_eth
                                wifi_form)
