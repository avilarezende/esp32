idf_component_register(SRCS "esp_lcd_ili9341.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_lcd
                       PRIV_REQUIRES driver)
