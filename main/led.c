#include "led.h"

#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "LED";

#define LEDC_MODE        LEDC_LOW_SPEED_MODE
#define LEDC_TIMER_SEL   LEDC_TIMER_0
#define LEDC_CHAN_SEL    LEDC_CHANNEL_0
#define LEDC_RESOLUTION  LEDC_TIMER_12_BIT
#define LEDC_FREQ_HZ     5000
#define LEDC_MAX_DUTY    ((1 << 12) - 1)
/* Onboard LEDs are usually wired as: VCC -> resistor -> LED -> GPIO,
 * so a low GPIO level lights it up (active low). */
#define LED_ACTIVE_LOW    1

static bool s_enabled = true;
static uint8_t s_brightness = 40;

static void led_apply(void)
{
    /* "level" is how much light we want, 0 = off, LEDC_MAX_DUTY = full */
    uint32_t level = s_enabled ? ((uint32_t)LEDC_MAX_DUTY * s_brightness / 100) : 0;
#if LED_ACTIVE_LOW
    uint32_t duty = LEDC_MAX_DUTY - level;
#else
    uint32_t duty = level;
#endif
    ledc_set_duty(LEDC_MODE, LEDC_CHAN_SEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHAN_SEL);
}

void led_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = LEDC_RESOLUTION,
        .timer_num = LEDC_TIMER_SEL,
        .freq_hz = LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t chan_cfg = {
        .gpio_num = LED_GPIO,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHAN_SEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_SEL,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&chan_cfg));

    led_apply();
    ESP_LOGI(TAG, "LED on GPIO%d, %d Hz PWM, brightness %d%% (%s)",
             LED_GPIO, LEDC_FREQ_HZ, s_brightness, s_enabled ? "on" : "off");
}

void led_set_enabled(bool on)
{
    s_enabled = on;
    led_apply();
}

void led_set_brightness(uint8_t percent)
{
    s_brightness = percent > 100 ? 100 : percent;
    led_apply();
}

bool led_get_enabled(void)
{
    return s_enabled;
}

uint8_t led_get_brightness(void)
{
    return s_brightness;
}
