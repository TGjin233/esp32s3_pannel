#include "relay_page.h"

#include "app_mqtt.h"
#include "cn_fonts.h"
#include "lvgl.h"

#define RELAY_COUNT 4

static lv_obj_t *s_screen;
static lv_obj_t *s_main_screen;
static lv_obj_t *s_cards[RELAY_COUNT];
static lv_obj_t *s_names[RELAY_COUNT];
static lv_obj_t *s_states[RELAY_COUNT];
static lv_obj_t *s_all_card;
static lv_obj_t *s_all_state;
static lv_obj_t *s_mqtt;
static volatile bool s_open;
static bool s_inited;

static void relay_click_cb(lv_event_t *e)
{
    int idx = (int)(lv_uintptr_t)lv_event_get_user_data(e);
    app_mqtt_set_relay(idx, !app_mqtt_get_relay(idx));
}

static void all_click_cb(lv_event_t *e)
{
    (void)e;
    app_mqtt_set_relay(4, !app_mqtt_all_relays_on());
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    relay_page_close();
}

static void apply_style(lv_obj_t *card, lv_obj_t *state, bool on, const char *on_text, const char *off_text)
{
    lv_obj_set_style_bg_color(card, on ? lv_color_hex(0x2ECC71) : lv_color_hex(0x1B2733), 0);
    lv_obj_set_style_shadow_width(card, on ? 8 : 0, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x2ECC71), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
    lv_label_set_text(state, on ? on_text : off_text);
    lv_obj_set_style_text_color(state, on ? lv_color_white() : lv_color_hex(0xB0BEC5), 0);
}

static void relay_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (!s_open) {
        return;
    }

    for (int i = 0; i < RELAY_COUNT; i++) {
        apply_style(s_cards[i], s_states[i], app_mqtt_get_relay(i), "开", "关");
        bool on = app_mqtt_get_relay(i);
        lv_obj_set_style_text_color(s_names[i], on ? lv_color_white() : lv_color_hex(0xE0E0E0), 0);
    }

    apply_style(s_all_card, s_all_state, app_mqtt_all_relays_on(), "全部开启", "全部关闭");

    bool online = app_mqtt_is_connected();
    lv_label_set_text(s_mqtt, online ? "MQTT: 在线" : "MQTT: 离线");
    lv_obj_set_style_text_color(s_mqtt, online ? lv_color_hex(0x66BB6A) : lv_color_hex(0xEF5350), 0);
}

static lv_obj_t *make_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *card = lv_btn_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x263442), 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1B2733), 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x2ECC71), 0);
    return card;
}

static void create_ui(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x0B1016), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_set_style_text_font(s_screen, &lv_font_cn_16, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, "继电器控制");
    lv_obj_set_style_text_font(title, &lv_font_cn_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x4FC3F7), 0);
    lv_obj_set_pos(title, 12, 6);

    s_mqtt = lv_label_create(s_screen);
    lv_label_set_text(s_mqtt, "MQTT: 离线");
    lv_obj_set_style_text_color(s_mqtt, lv_color_hex(0xEF5350), 0);
    lv_obj_align(s_mqtt, LV_ALIGN_TOP_RIGHT, -12, 12);

    for (int i = 0; i < RELAY_COUNT; i++) {
        lv_coord_t x = (i % 2) ? 164 : 8;
        lv_coord_t y = (i / 2) ? 96 : 38;

        lv_obj_t *card = make_card(s_screen, x, y, 148, 54);
        lv_obj_add_event_cb(card, relay_click_cb, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)i);

        lv_obj_t *name = lv_label_create(card);
        lv_label_set_text_fmt(name, "继电器 %d", i + 1);
        lv_obj_set_style_text_color(name, lv_color_hex(0xE0E0E0), 0);
        lv_obj_align(name, LV_ALIGN_CENTER, 0, -13);

        lv_obj_t *state = lv_label_create(card);
        lv_label_set_text(state, "关");
        lv_obj_set_style_text_font(state, &lv_font_cn_20, 0);
        lv_obj_set_style_text_color(state, lv_color_hex(0x90A4AE), 0);
        lv_obj_align(state, LV_ALIGN_CENTER, 0, 9);

        s_cards[i] = card;
        s_names[i] = name;
        s_states[i] = state;
    }

    s_all_card = make_card(s_screen, 8, 154, 304, 36);
    lv_obj_add_event_cb(s_all_card, all_click_cb, LV_EVENT_CLICKED, NULL);

    s_all_state = lv_label_create(s_all_card);
    lv_label_set_text(s_all_state, "全部关闭");
    lv_obj_set_style_text_font(s_all_state, &lv_font_cn_20, 0);
    lv_obj_center(s_all_state);

    lv_obj_t *back = make_card(s_screen, 8, 198, 84, 32);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x263442), 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_label = lv_label_create(back);
    lv_label_set_text(back_label, "返回");
    lv_obj_center(back_label);

    lv_obj_t *hint = lv_label_create(s_screen);
    lv_label_set_text(hint, "控制设备: esp32");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x607D8B), 0);
    lv_obj_set_pos(hint, 104, 206);
}

void relay_page_open(void)
{
    if (!s_inited) {
        s_inited = true;
        create_ui();
        lv_timer_create(relay_timer_cb, 200, NULL);
    }

    if (s_main_screen == NULL) {
        s_main_screen = lv_scr_act();
    }

    lv_scr_load(s_screen);
    s_open = true;
}

void relay_page_close(void)
{
    s_open = false;
    if (s_main_screen != NULL) {
        lv_scr_load(s_main_screen);
    }
}

bool relay_page_is_open(void)
{
    return s_open;
}
