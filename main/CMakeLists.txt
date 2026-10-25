idf_component_register(SRCS "main.c"
                            "lcd_ili9341.c"
                            "lv_port_disp.c"
                            "lv_port_indev.c"
                            "xpt2046.c"
                            "led.c"
                            "app_mqtt.c"
                            "album.c"
                            "relay_page.c"
                            "ui.c"
                            "lv_font_cn_16.c"
                            "lv_font_cn_20.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_lcd esp_timer
                       PRIV_REQUIRES driver lvgl__lvgl espressif__esp_lcd_ili9341 spi_flash esp_psram
                                    mqtt json esp_wifi esp_event esp_netif nvs_flash
                                    esp_http_client espressif__esp_jpeg)

# lv_font_conv generated sources include "lvgl.h" directly
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
