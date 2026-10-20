idf_component_register(SRCS "main.c"
                            "wifi_manager.c"
                            "http_server.c"
                            "qemu_eth.c"
                            "assistant.c"
                            "ha_client.c"
                            "cyd_scene.c"
                            "cyd_display.c"
                            "cyd_splash.c"
                            "cyd_camera.c"
                            "cyd_cfg.c"
                            "cyd_bmp.c"
                            "cyd_net.c"
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES "app.html"
                       REQUIRES spi_flash
                                app_update
                                esp_https_ota
                                mbedtls
                                esp_wifi
                                esp_lcd
                                esp_lcd_ili9341
                                driver
                                esp_timer
                                esp_http_client
                                esp_http_server
                                esp_netif
                                lwip
                                esp_event
                                nvs_flash
                                esp_eth
                                wifi_form)
