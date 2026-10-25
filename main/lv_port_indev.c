#include "lv_port_indev.h"

#include "lvgl.h"
#include "xpt2046.h"

static int16_t s_last_x;
static int16_t s_last_y;
static bool s_last_pressed;

static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;

    int16_t x, y;
    if (xpt2046_read_point(&x, &y)) {
        s_last_x = x;
        s_last_y = y;
        s_last_pressed = true;
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PR;
    } else {
        s_last_pressed = false;
        data->state = LV_INDEV_STATE_REL;
    }
}

void lv_port_indev_init(void)
{
    static lv_indev_drv_t indev_drv;

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&indev_drv);
}

bool lv_port_indev_last_point(int16_t *x, int16_t *y)
{
    if (!s_last_pressed) {
        return false;
    }
    *x = s_last_x;
    *y = s_last_y;
    return true;
}
