#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* XPT2046 wiring on this board */
#define XPT2046_PIN_CLK  6   /* DCLK */
#define XPT2046_PIN_CS   5   /* CS   */
#define XPT2046_PIN_DIN  4   /* MOSI */
#define XPT2046_PIN_DO   3   /* MISO */
#define XPT2046_PIN_IRQ  2   /* PENIRQ, low when touched */

/* Raw ADC calibration range (12-bit, 0..4095 of the useful area).
 * Reduce/increase the range if the touch area does not reach the screen edges. */
#define XPT2046_RAW_X_MIN 200
#define XPT2046_RAW_X_MAX 3900
#define XPT2046_RAW_Y_MIN 200
#define XPT2046_RAW_Y_MAX 3900

/* Axis mapping, adjust so touch coordinates match the landscape screen:
 * swap: raw X drives screen Y (needed after 90 degree panel rotation)
 * mirror: invert that screen axis (both mirror bits = 180 degree rotation) */
#define XPT2046_SWAP_XY  1
#define XPT2046_MIRROR_X 1
#define XPT2046_MIRROR_Y 1

void xpt2046_init(void);
bool xpt2046_is_pressed(void);
/* Read raw ADC channels, returns false if not pressed or reading is invalid */
bool xpt2046_read_raw(uint16_t *x, uint16_t *y);
/* Read touch position mapped to screen pixels (0..319, 0..239) */
bool xpt2046_read_point(int16_t *x, int16_t *y);

#ifdef __cplusplus
}
#endif
