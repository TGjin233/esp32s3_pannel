#include <inttypes.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"

#include "app_mqtt.h"
#include "lcd_ili9341.h"
#include "led.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "ui.h"
#include "xpt2046.h"

static const char *TAG = "APP";

void app_main(void)
{
    esp_chip_info_t chip_info = {0};
    esp_chip_info(&chip_info);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    ESP_LOGI(TAG, "ESP-IDF: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "Chip: ESP32-S3, cores: %d, revision: %d", chip_info.cores, chip_info.revision);
    ESP_LOGI(TAG, "Flash: %" PRIu32 " MB", flash_size / (1024 * 1024));
    if (esp_psram_is_initialized()) {
        ESP_LOGI(TAG, "PSRAM: %u MB", esp_psram_get_size() / (1024 * 1024));
    } else {
        ESP_LOGW(TAG, "PSRAM not initialized");
    }

    lcd_init();
    xpt2046_init();
    led_init();
    lv_port_disp_init();
    lv_port_indev_init();
    ui_init();
    app_mqtt_start();

    /* Turn the backlight on after the first frame is likely drawn */
    vTaskDelay(pdMS_TO_TICKS(100));
    lcd_backlight(true);
}
