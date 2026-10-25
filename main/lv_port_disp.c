#include "lv_port_disp.h"

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lcd_ili9341.h"
#include "lvgl.h"

static const char *TAG = "LVGL";

/* LVGL tick period, the refresh period is set by CONFIG_LV_DISP_DEF_REFR_PERIOD */
#define LVGL_TICK_PERIOD_MS   2
/* Max sleep of the LVGL task when nothing is pending */
#define LVGL_TASK_MAX_DELAY   20
#define LVGL_TASK_STACK_SIZE  8192
#define LVGL_TASK_PRIORITY    4
/* Draw buffer height in lines */
#define LVGL_DRAW_BUF_LINES   40
#define LVGL_DRAW_BUF_PIXELS  (LCD_H_RES * LVGL_DRAW_BUF_LINES)
/* Max time to wait for one DMA transfer */
#define LVGL_FLUSH_TIMEOUT_MS 100

static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;
static esp_timer_handle_t s_tick_timer;
static SemaphoreHandle_t s_flush_sem;

static lv_color_t *lvgl_buf_alloc(size_t pixels)
{
    size_t bytes = pixels * sizeof(lv_color_t);
    /* Prefer internal DMA capable RAM, PSRAM is a fallback */
    lv_color_t *buf = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (buf == NULL) {
        buf = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM);
    }
    if (buf != NULL) {
        ESP_LOGI(TAG, "draw buffer: %u bytes @%p", (unsigned)bytes, buf);
    }
    return buf;
}

/* Called from the SPI ISR when the color data transfer is done */
static bool lcd_color_done_isr(void *user_ctx)
{
    (void)user_ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_sem, &woken);
    return woken == pdTRUE;
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    if (area->x2 < area->x1 || area->y2 < area->y1) {
        lv_disp_flush_ready(drv);
        return;
    }

    /* drop a possibly stale completion signal */
    (void)xSemaphoreTake(s_flush_sem, 0);

    esp_err_t err = esp_lcd_panel_draw_bitmap(lcd_panel(), area->x1, area->y1,
                                             area->x2 + 1, area->y2 + 1, color_p);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "draw_bitmap failed: %s", esp_err_to_name(err));
        lv_disp_flush_ready(drv);
        return;
    }

    /* The DMA transfer is async, block here until it is really done so the
     * draw buffer is not reused while the SPI engine is still reading it. */
    if (xSemaphoreTake(s_flush_sem, pdMS_TO_TICKS(LVGL_FLUSH_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "flush timeout, area (%d,%d)-(%d,%d)",
                 area->x1, area->y1, area->x2, area->y2);
    }

    lv_disp_flush_ready(drv);
}

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_task(void *arg)
{
    (void)arg;
    while (1) {
        uint32_t delay_ms = lv_timer_handler();
        if (delay_ms == 0) {
            delay_ms = 1;
        }
        if (delay_ms > LVGL_TASK_MAX_DELAY) {
            delay_ms = LVGL_TASK_MAX_DELAY;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

void lv_port_disp_init(void)
{
    lv_init();

    s_flush_sem = xSemaphoreCreateBinary();
    if (s_flush_sem == NULL) {
        ESP_LOGE(TAG, "failed to create flush semaphore");
        abort();
    }

    lv_color_t *buf1 = lvgl_buf_alloc(LVGL_DRAW_BUF_PIXELS);
    if (buf1 == NULL) {
        ESP_LOGE(TAG, "no memory for LVGL draw buffer");
        abort();
    }
    lv_disp_draw_buf_init(&s_draw_buf, buf1, NULL, LVGL_DRAW_BUF_PIXELS);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = LCD_H_RES;
    s_disp_drv.ver_res = LCD_V_RES;
    s_disp_drv.flush_cb = lvgl_flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_t *disp = lv_disp_drv_register(&s_disp_drv);
    if (disp == NULL) {
        ESP_LOGE(TAG, "failed to register display driver");
        abort();
    }

    lcd_set_color_done_cb(lcd_color_done_isr, NULL);

    esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &s_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    xTaskCreate(lvgl_task, "lvgl", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "LVGL %d.%d.%d initialized (%d x %d, %d lines buffer)",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
             LCD_H_RES, LCD_V_RES, LVGL_DRAW_BUF_LINES);
}
