#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ILI9341 wiring on this board */
#define LCD_PIN_CS    14
#define LCD_PIN_RST   13
#define LCD_PIN_DC    12
#define LCD_PIN_MOSI  11
#define LCD_PIN_SCLK  10
#define LCD_PIN_BL    9
#define LCD_PIN_MISO  8

/* Landscape resolution (240x320 rotated 90 degrees) */
#define LCD_H_RES 320
#define LCD_V_RES 240

/* Called in SPI ISR context when a color transfer finished */
typedef bool (*lcd_color_done_cb_t)(void *user_ctx);

void lcd_init(void);
void lcd_set_color_done_cb(lcd_color_done_cb_t cb, void *user_ctx);
esp_lcd_panel_handle_t lcd_panel(void);
void lcd_backlight(bool on);

#ifdef __cplusplus
}
#endif
