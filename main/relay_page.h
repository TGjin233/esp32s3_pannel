#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void relay_page_open(void);
void relay_page_close(void);
bool relay_page_is_open(void);

#ifdef __cplusplus
}
#endif
