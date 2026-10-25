#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* LED on this board */
#define LED_GPIO 48

void led_init(void);
void led_set_enabled(bool on);
/* brightness in percent, 0..100 */
void led_set_brightness(uint8_t percent);
bool led_get_enabled(void);
uint8_t led_get_brightness(void);

#ifdef __cplusplus
}
#endif
