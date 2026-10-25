#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void app_mqtt_start(void);
bool app_mqtt_is_connected(void);
/* cached WiFi RSSI in dBm, 0 when not connected */
int app_mqtt_get_rssi(void);
int app_mqtt_get_uptime_sec(void);

/* Remote relay control (target device publishes on iot/<RELAY_TARGET_KEY>/data) */
bool app_mqtt_get_relay(int idx);      /* idx 0..3 */
bool app_mqtt_all_relays_on(void);
void app_mqtt_set_relay(int idx, bool on); /* idx 0..3, 4 = all */

#ifdef __cplusplus
}
#endif
