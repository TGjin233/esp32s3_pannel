#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void lv_port_indev_init(void);
/* Returns true if the panel is currently touched, and the last point in screen pixels */
bool lv_port_indev_last_point(int16_t *x, int16_t *y);

#ifdef __cplusplus
}
#endif
