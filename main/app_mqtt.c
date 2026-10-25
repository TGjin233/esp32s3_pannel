#include "app_mqtt.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "cJSON.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "mqtt_client.h"
#include "nvs_flash.h"

static const char *TAG = "MQTT";

#define CMD_TOPIC          "iot/" DEVICE_KEY "/cmd"
#define DATA_TOPIC         "iot/" DEVICE_KEY "/data"
#define TARGET_CMD_TOPIC   "iot/" RELAY_TARGET_KEY "/cmd"
#define TARGET_DATA_TOPIC  "iot/" RELAY_TARGET_KEY "/data"
#define REPORT_INTERVAL_MS 5000

static esp_mqtt_client_handle_t s_client;
static volatile bool s_mqtt_connected;
static volatile bool s_mqtt_started;
static volatile bool s_report_now;
static volatile int s_rssi;
static volatile bool s_relay[4];

static bool topic_is(const char *topic, int topic_len, const char *expected)
{
    size_t len = strlen(expected);
    return ((size_t)topic_len == len) && (strncmp(topic, expected, len) == 0);
}

static void publish_json(const char *topic, const char *payload)
{
    if (s_client == NULL || !s_mqtt_connected) {
        return;
    }
    esp_mqtt_client_publish(s_client, topic, payload, 0, 0, 0);
}

bool app_mqtt_get_relay(int idx)
{
    if (idx < 0 || idx >= 4) {
        return false;
    }
    return s_relay[idx];
}

bool app_mqtt_all_relays_on(void)
{
    for (int i = 0; i < 4; i++) {
        if (!s_relay[i]) {
            return false;
        }
    }
    return true;
}

void app_mqtt_set_relay(int idx, bool on)
{
    char payload[32];

    if (idx >= 4) {
        snprintf(payload, sizeof(payload), "{\"relayall\":%d}", on ? 1 : 0);
        for (int i = 0; i < 4; i++) {
            s_relay[i] = on;
        }
    } else if (idx >= 0) {
        snprintf(payload, sizeof(payload), "{\"relay%d\":%d}", idx + 1, on ? 1 : 0);
        s_relay[idx] = on;
    } else {
        return;
    }

    publish_json(TARGET_CMD_TOPIC, payload);
    ESP_LOGI(TAG, "relay cmd -> %s", payload);
}

bool app_mqtt_is_connected(void)
{
    return s_mqtt_connected;
}

int app_mqtt_get_rssi(void)
{
    return s_rssi;
}

int app_mqtt_get_uptime_sec(void)
{
    return (int)(esp_timer_get_time() / 1000000);
}

static void publish_state(void)
{
    if (s_client == NULL) {
        return;
    }

    int uptime = app_mqtt_get_uptime_sec();
    char payload[160];
    snprintf(payload, sizeof(payload),
             "{\"switch\":%d,\"pwm\":%d,\"rssi\":%d,\"uptime\":%d}",
             led_get_enabled() ? 1 : 0,
             led_get_brightness() * 255 / 100,
             s_rssi, uptime);

    esp_mqtt_client_publish(s_client, DATA_TOPIC, payload, 0, 0, 0);
    ESP_LOGI(TAG, "report: %s", payload);
}

static void handle_cmd(const char *data, int len)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (root == NULL) {
        ESP_LOGW(TAG, "bad cmd json: %.*s", len, data);
        return;
    }

    const cJSON *sw = cJSON_GetObjectItemCaseSensitive(root, "switch");
    if (cJSON_IsNumber(sw)) {
        led_set_enabled(sw->valueint != 0);
        ESP_LOGI(TAG, "cmd switch=%d", sw->valueint);
    }

    const cJSON *pwm = cJSON_GetObjectItemCaseSensitive(root, "pwm");
    if (cJSON_IsNumber(pwm)) {
        int value = pwm->valueint;
        /* accept both 0..100 (percent) and 0..255 (raw pwm) */
        int percent = (value > 100) ? (value * 100 + 127) / 255 : value;
        if (percent > 100) {
            percent = 100;
        }
        if (percent < 0) {
            percent = 0;
        }
        led_set_brightness((uint8_t)percent);
        ESP_LOGI(TAG, "cmd pwm=%d -> %d%%", value, percent);
    }

    cJSON_Delete(root);

    /* echo the new state right away so the web console updates */
    s_report_now = true;
}

static void handle_target_data(const char *data, int len)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (root == NULL) {
        return;
    }

    bool have_individual = false;

    for (int i = 0; i < 4; i++) {
        char key[8];
        snprintf(key, sizeof(key), "relay%d", i + 1);
        const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
        if (cJSON_IsNumber(item)) {
            s_relay[i] = (item->valueint != 0);
            have_individual = true;
        }
    }

    /* "relayall" in a status report is only a summary flag (it becomes 0 as
     * soon as one relay is off), so it must not overwrite the single relays
     * when they are present. When it is the only field, only "1" is
     * meaningful (all on); "0" is ambiguous so nothing is forced. */
    if (!have_individual) {
        const cJSON *all = cJSON_GetObjectItemCaseSensitive(root, "relayall");
        if (cJSON_IsNumber(all) && all->valueint != 0) {
            for (int i = 0; i < 4; i++) {
                s_relay[i] = true;
            }
        }
    }

    cJSON_Delete(root);
}

static void mqtt_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected, client id: %s", DEVICE_KEY);
        esp_mqtt_client_subscribe(event->client, CMD_TOPIC, 0);
        esp_mqtt_client_subscribe(event->client, TARGET_DATA_TOPIC, 0);
        s_mqtt_connected = true;
        s_report_now = true;
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected");
        s_mqtt_connected = false;
        break;
    case MQTT_EVENT_DATA:
        if (topic_is(event->topic, event->topic_len, TARGET_DATA_TOPIC)) {
            handle_target_data(event->data, event->data_len);
        } else {
            ESP_LOGI(TAG, "cmd on %.*s: %.*s", event->topic_len, event->topic,
                     event->data_len, event->data);
            handle_cmd(event->data, event->data_len);
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "mqtt error");
        break;
    default:
        break;
    }
}

static void mqtt_start(void)
{
    if (s_mqtt_started) {
        return;
    }
    s_mqtt_started = true;

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_URI,
        .credentials.username = MQTT_USER,
        .credentials.authentication.password = MQTT_PASS,
        .credentials.client_id = DEVICE_KEY,
        .session.keepalive = 30,
        .network.reconnect_timeout_ms = 5000,
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "failed to init mqtt client");
        s_mqtt_started = false;
        return;
    }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);
}

static void report_task(void *arg)
{
    (void)arg;
    int64_t next_report = 0;
    int64_t next_rssi = 0;

    while (1) {
        int64_t now = esp_timer_get_time();

        if (now >= next_rssi) {
            wifi_ap_record_t ap;
            s_rssi = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : 0;
            next_rssi = now + 1000000;
        }

        if (s_mqtt_connected) {
            if (s_report_now || now >= next_report) {
                publish_state();
                s_report_now = false;
                next_report = now + (int64_t)REPORT_INTERVAL_MS * 1000;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "wifi disconnected, retrying");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "wifi connected, ip: " IPSTR, IP2STR(&event->ip_info.ip));
        mqtt_start();
    }
}

void app_mqtt_start(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, NULL));

    wifi_config_t sta_cfg = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    xTaskCreate(report_task, "mqtt_report", 4096, NULL, 4, NULL);

    ESP_LOGI(TAG, "connecting wifi '%s', mqtt %s as '%s'", WIFI_SSID, MQTT_URI, DEVICE_KEY);
}
