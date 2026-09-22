idf_component_register(SRCS "main.c"
                            "wifi_manager.c"
                            "http_server.c"
                            "qemu_eth.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi
                                esp_http_server
                                esp_netif
                                esp_event
                                nvs_flash
                                esp_eth
                                wifi_form)
