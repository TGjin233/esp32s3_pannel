#include "ui.h"

#include "album.h"
#include "app_mqtt.h"
#include "cn_fonts.h"
#include "led.h"
#include "lv_port_indev.h"
#include "lvgl.h"
#include "relay_page.h"

static lv_obj_t *s_dot;
static lv_obj_t *s_status;
static lv_obj_t *s_mqtt_status;
static lv_obj_t *s_led_value;
static lv_obj_t *s_power_btn;
static lv_obj_t *s_power_icon;
static lv_obj_t *s_led_state;
static lv_obj_t *s_led_slider;
static lv_obj_t *s_info;
static bool s_power_on = true;
static int s_tick_count;

static lv_obj_t *make_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x16202B), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x263442), 0);
    return card;
}

static void slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t value = lv_slider_get_value(slider);

    led_set_brightness((uint8_t)value);
    lv_label_set_text_fmt(s_led_value, "%d%%", (int)value);
}

static void power_btn_apply(bool on)
{
    lv_obj_set_style_bg_color(s_power_btn, on ? lv_color_hex(0x2ECC71) : lv_color_hex(0x2A3644), 0);
    lv_obj_set_style_shadow_width(s_power_btn, on ? 14 : 0, 0);
    lv_obj_set_style_text_color(s_power_icon, on ? lv_color_white() : lv_color_hex(0x78909C), 0);
    lv_label_set_text(s_led_state, on ? "开" : "关");
    lv_obj_set_style_text_color(s_led_state, on ? lv_color_hex(0x66BB6A) : lv_color_hex(0xEF5350), 0);
}

static void power_btn_cb(lv_event_t *e)
{
    (void)e;
    s_power_on = !led_get_enabled();
    led_set_enabled(s_power_on);
    power_btn_apply(s_power_on);
}

static void album_btn_cb(lv_event_t *e)
{
    (void)e;
    album_open();
}

static void relay_btn_cb(lv_event_t *e)
{
    (void)e;
    relay_page_open();
}

/* Runs in the LVGL task: safe place to touch widgets */
static void ui_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    int16_t x, y;
    if (lv_port_indev_last_point(&x, &y)) {
        lv_obj_clear_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_dot, x - 8, y - 8);
        lv_label_set_text_fmt(s_status, "X: %3d   Y: %3d   按下", x, y);
    } else {
        lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_status, "X: ---   Y: ---   抬起");
    }

    /* keep the widgets in sync with commands coming from MQTT */
    bool enabled = led_get_enabled();
    if (enabled != s_power_on) {
        s_power_on = enabled;
        power_btn_apply(enabled);
    }

    uint8_t brightness = led_get_brightness();
    if ((int32_t)brightness != lv_slider_get_value(s_led_slider)) {
        lv_slider_set_value(s_led_slider, brightness, LV_ANIM_OFF);
        lv_label_set_text_fmt(s_led_value, "%d%%", brightness);
    }

    bool online = app_mqtt_is_connected();
    lv_label_set_text(s_mqtt_status, online ? "MQTT: 在线" : "MQTT: 离线");
    lv_obj_set_style_text_color(s_mqtt_status,
                                online ? lv_color_hex(0x66BB6A) : lv_color_hex(0xEF5350), 0);

    if (++s_tick_count >= 10) {
        s_tick_count = 0;
        int rssi = app_mqtt_get_rssi();
        lv_label_set_text_fmt(s_info, "信号 %d  运行 %ds", rssi, app_mqtt_get_uptime_sec());
    }
}

void ui_init(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B1016), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_text_font(scr, &lv_font_cn_16, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* header */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "ESP32-S3 控制面板");
    lv_obj_set_style_text_font(title, &lv_font_cn_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x4FC3F7), 0);
    lv_obj_set_pos(title, 12, 6);

    s_mqtt_status = lv_label_create(scr);
    lv_label_set_text(s_mqtt_status, "MQTT: 离线");
    lv_obj_set_style_text_color(s_mqtt_status, lv_color_hex(0xEF5350), 0);
    lv_obj_align(s_mqtt_status, LV_ALIGN_TOP_RIGHT, -12, 12);

    /* LED card */
    lv_obj_t *led_card = make_card(scr, 8, 38, 304, 88);

    lv_obj_t *led_title = lv_label_create(led_card);
    lv_label_set_text(led_title, "灯光控制");
    lv_obj_set_style_text_color(led_title, lv_color_hex(0x90A4AE), 0);
    lv_obj_set_pos(led_title, 14, 8);

    s_power_btn = lv_btn_create(led_card);
    lv_obj_set_pos(s_power_btn, 14, 30);
    lv_obj_set_size(s_power_btn, 48, 48);
    lv_obj_set_style_radius(s_power_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_power_btn, 0, 0);
    lv_obj_set_style_shadow_color(s_power_btn, lv_color_hex(0x2ECC71), 0);
    lv_obj_set_style_shadow_opa(s_power_btn, LV_OPA_40, 0);
    lv_obj_set_style_shadow_ofs_y(s_power_btn, 2, 0);
    lv_obj_add_event_cb(s_power_btn, power_btn_cb, LV_EVENT_CLICKED, NULL);

    s_power_icon = lv_label_create(s_power_btn);
    lv_label_set_text(s_power_icon, LV_SYMBOL_POWER);
    lv_obj_set_style_text_font(s_power_icon, &lv_font_montserrat_20, 0);
    lv_obj_center(s_power_icon);

    s_led_state = lv_label_create(led_card);
    lv_label_set_text(s_led_state, "开");
    lv_obj_set_style_text_font(s_led_state, &lv_font_cn_20, 0);
    lv_obj_set_pos(s_led_state, 74, 34);

    lv_obj_t *bright_label = lv_label_create(led_card);
    lv_label_set_text(bright_label, "亮度");
    lv_obj_set_style_text_color(bright_label, lv_color_hex(0x90A4AE), 0);
    lv_obj_set_pos(bright_label, 74, 64);

    s_led_slider = lv_slider_create(led_card);
    lv_obj_set_size(s_led_slider, 108, 12);
    lv_obj_set_pos(s_led_slider, 142, 68);
    lv_slider_set_range(s_led_slider, 0, 100);
    lv_slider_set_value(s_led_slider, 40, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_led_slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_led_value = lv_label_create(led_card);
    lv_label_set_text(s_led_value, "40%");
    lv_obj_set_style_text_color(s_led_value, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_pos(s_led_value, 256, 62);

    power_btn_apply(s_power_on);

    /* touch card */
    lv_obj_t *touch_card = make_card(scr, 8, 134, 304, 62);

    lv_obj_t *touch_title = lv_label_create(touch_card);
    lv_label_set_text(touch_title, "触摸坐标");
    lv_obj_set_style_text_color(touch_title, lv_color_hex(0x90A4AE), 0);
    lv_obj_set_pos(touch_title, 14, 8);

    s_status = lv_label_create(touch_card);
    lv_label_set_text(s_status, "X: ---   Y: ---   抬起");
    lv_obj_set_style_text_color(s_status, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_pos(s_status, 14, 32);

    /* page switches */
    lv_obj_t *relay_btn = lv_btn_create(scr);
    lv_obj_set_size(relay_btn, 74, 32);
    lv_obj_set_pos(relay_btn, 8, 200);
    lv_obj_add_event_cb(relay_btn, relay_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *relay_label = lv_label_create(relay_btn);
    lv_label_set_text(relay_label, "继电器");
    lv_obj_center(relay_label);

    lv_obj_t *album_btn = lv_btn_create(scr);
    lv_obj_set_size(album_btn, 74, 32);
    lv_obj_set_pos(album_btn, 88, 200);
    lv_obj_add_event_cb(album_btn, album_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *album_label = lv_label_create(album_btn);
    lv_label_set_text(album_label, "相册");
    lv_obj_center(album_label);

    /* footer info */
    s_info = lv_label_create(scr);
    lv_label_set_text(s_info, "信号 --  运行 0s");
    lv_obj_set_style_text_color(s_info, lv_color_hex(0x607D8B), 0);
    lv_obj_set_pos(s_info, 172, 208);

    /* touch indicator, must be the top-most widget */
    s_dot = lv_obj_create(scr);
    lv_obj_set_size(s_dot, 16, 16);
    lv_obj_clear_flag(s_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(0xFFB300), 0);
    lv_obj_set_style_border_width(s_dot, 0, 0);
    lv_obj_set_style_pad_all(s_dot, 0, 0);
    lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN);

    lv_timer_create(ui_timer_cb, 100, NULL);
}
