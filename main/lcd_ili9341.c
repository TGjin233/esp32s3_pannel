#include "lcd_ili9341.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_ili9341.h"
#include "esp_log.h"

/* SPI clock: 40MHz is safe for most ILI9341 modules.
 * Try 60/80MHz if your module is stable, refresh rate scales linearly. */
#define LCD_SPI_CLK_HZ   (40 * 1000 * 1000)
#define LCD_SPI_HOST     SPI2_HOST
#define LCD_BL_ON_LEVEL  1
/* Max bytes of a single DMA transfer, must be >= largest LVGL draw buffer */
#define LCD_MAX_TRANS_SZ (LCD_H_RES * 80 * sizeof(uint16_t))

static const char *TAG = "LCD";

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static lcd_color_done_cb_t s_done_cb;
static void *s_done_ctx;

static bool lcd_io_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    (void)io;
    (void)edata;
    (void)user_ctx;
    if (s_done_cb) {
        return s_done_cb(s_done_ctx);
    }
    return false;
}

void lcd_set_color_done_cb(lcd_color_done_cb_t cb, void *user_ctx)
{
    s_done_cb = cb;
    s_done_ctx = user_ctx;
}

void lcd_init(void)
{
    gpio_config_t bl_cfg = {
        .pin_bit_mask = 1ULL << LCD_PIN_BL,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&bl_cfg));
    gpio_set_level(LCD_PIN_BL, !LCD_BL_ON_LEVEL);

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = LCD_PIN_MISO,
        .sclk_io_num = LCD_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_MAX_TRANS_SZ,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_PIN_CS,
        .dc_gpio_num = LCD_PIN_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_SPI_CLK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_cfg, &s_io));

    esp_lcd_panel_io_callbacks_t io_cbs = {
        .on_color_trans_done = lcd_io_color_done,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(s_io, &io_cbs, NULL));

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(s_io, &panel_cfg, &s_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

    /* Landscape with swap_xy enabled.
     * With MV=1 the row/column axes are exchanged, so in this mode:
     *   mirror_x (MX) flips vertically, mirror_y (MY) flips horizontally.
     * (false, false) = plain landscape, (true, true) = landscape rotated 180. */
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, false, false));
    /* Set to true only if the panel shows a negative (inverted) image */
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    ESP_LOGI(TAG, "ILI9341 ready: %dx%d landscape (swap_xy=1, mirror_x=0, mirror_y=0), SPI %d MHz",
             LCD_H_RES, LCD_V_RES, LCD_SPI_CLK_HZ / 1000000);
}

esp_lcd_panel_handle_t lcd_panel(void)
{
    return s_panel;
}

void lcd_backlight(bool on)
{
    gpio_set_level(LCD_PIN_BL, on ? LCD_BL_ON_LEVEL : !LCD_BL_ON_LEVEL);
}
