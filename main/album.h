#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void album_open(void);
void album_close(void);
bool album_is_open(void);

#ifdef __cplusplus
}
#endif
