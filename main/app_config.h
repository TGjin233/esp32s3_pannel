#pragma once

/* WiFi */
#define WIFI_SSID   "5019"
#define WIFI_PASS   "5019yyds"

/* MQTT server */
#define MQTT_HOST   "47.111.142.84"
#define MQTT_PORT   1883
#define MQTT_USER   "device"
#define MQTT_PASS   "5019yyds"
#define DEVICE_KEY  "esp32s3"

#define MQTT_URI    "mqtt://" MQTT_HOST ":1883"

/* Remote device controlled from this panel (relay1..relay4, relayall) */
#define RELAY_TARGET_KEY "esp32"

/* Album HTTP service (album-service, port 8888) */
#define ALBUM_BASE_URL "http://" MQTT_HOST ":8888"
