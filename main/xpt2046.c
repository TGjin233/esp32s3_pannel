#include "xpt2046.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "lcd_ili9341.h"

static const char *TAG = "XPT2046";

/* XPT2046 control bytes: 12-bit, differential, power-down between conversions */
#define XPT_CMD_X   0xD0
#define XPT_CMD_Y   0x90

#define XPT_CS_LOW()   gpio_set_level(XPT2046_PIN_CS, 0)
#define XPT_CS_HIGH()  gpio_set_level(XPT2046_PIN_CS, 1)
#define XPT_CLK_LOW()  gpio_set_level(XPT2046_PIN_CLK, 0)
#define XPT_CLK_HIGH() gpio_set_level(XPT2046_PIN_CLK, 1)
#define XPT_DIN(v)     gpio_set_level(XPT2046_PIN_DIN, (v) ? 1 : 0)
#define XPT_DO()       gpio_get_level(XPT2046_PIN_DO)
#define XPT_DELAY()    esp_rom_delay_us(1)

/* Bit-banged SPI: send a command byte, then clock in 16 bits (12 useful) */
static uint16_t xpt_transfer(uint8_t cmd)
{
    uint32_t data = 0;

    XPT_CS_LOW();

    for (int i = 7; i >= 0; i--) {
        XPT_CLK_LOW();
        XPT_DIN((cmd >> i) & 0x01);
        XPT_DELAY();
        XPT_CLK_HIGH();
        XPT_DELAY();
    }

    for (int i = 0; i < 16; i++) {
        XPT_CLK_LOW();
        XPT_DELAY();
        XPT_CLK_HIGH();
        data = (data << 1) | XPT_DO();
        XPT_DELAY();
    }

    XPT_CS_HIGH();

    return (uint16_t)((data >> 3) & 0x0FFF);
}

void xpt2046_init(void)
{
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << XPT2046_PIN_CS) | (1ULL << XPT2046_PIN_CLK) | (1ULL << XPT2046_PIN_DIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&out_cfg));

    gpio_config_t in_cfg = {
        .pin_bit_mask = (1ULL << XPT2046_PIN_DO) | (1ULL << XPT2046_PIN_IRQ),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in_cfg));

    XPT_CS_HIGH();
    XPT_CLK_LOW();

    /* one dummy conversion to leave the controller in a known state */
    (void)xpt_transfer(XPT_CMD_X);

    ESP_LOGI(TAG, "XPT2046 ready: CLK=%d CS=%d DIN=%d DO=%d IRQ=%d",
             XPT2046_PIN_CLK, XPT2046_PIN_CS, XPT2046_PIN_DIN, XPT2046_PIN_DO, XPT2046_PIN_IRQ);
}

bool xpt2046_is_pressed(void)
{
    return gpio_get_level(XPT2046_PIN_IRQ) == 0;
}

bool xpt2046_read_raw(uint16_t *x, uint16_t *y)
{
    if (!xpt2046_is_pressed()) {
        return false;
    }

    uint16_t rx = xpt_transfer(XPT_CMD_X);
    uint16_t ry = xpt_transfer(XPT_CMD_Y);

    /* reject open/short readings */
    if (rx < 20 || rx > 4075 || ry < 20 || ry > 4075) {
        return false;
    }

    *x = rx;
    *y = ry;
    return true;
}

static int32_t map_range(int32_t v, int32_t in_min, int32_t in_max, int32_t out_max)
{
    if (v <= in_min) {
        return 0;
    }
    if (v >= in_max) {
        return out_max;
    }
    return (v - in_min) * out_max / (in_max - in_min);
}

bool xpt2046_read_point(int16_t *x, int16_t *y)
{
    uint16_t rx, ry;
    if (!xpt2046_read_raw(&rx, &ry)) {
        return false;
    }

    int32_t nx = map_range(rx, XPT2046_RAW_X_MIN, XPT2046_RAW_X_MAX, 4095);
    int32_t ny = map_range(ry, XPT2046_RAW_Y_MIN, XPT2046_RAW_Y_MAX, 4095);

#if XPT2046_SWAP_XY
    int32_t tmp = nx;
    nx = ny;
    ny = tmp;
#endif

    int32_t px = nx * (LCD_H_RES - 1) / 4095;
    int32_t py = ny * (LCD_V_RES - 1) / 4095;

#if XPT2046_MIRROR_X
    px = (LCD_H_RES - 1) - px;
#endif
#if XPT2046_MIRROR_Y
    py = (LCD_V_RES - 1) - py;
#endif

    *x = (int16_t)px;
    *y = (int16_t)py;
    return true;
}
