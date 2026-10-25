#include "album.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "cJSON.h"
#include "cn_fonts.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "extra/libs/png/lodepng.h"
#include "jpeg_decoder.h"
#include "lvgl.h"

static const char *TAG = "ALBUM";

#define ALB_MAX_IMAGES     20
#define ALB_URL_MAX        192
#define ALB_W              320
#define ALB_H              240
#define ALB_PIX_BYTES      (ALB_W * ALB_H * 2)
#define ALB_IMG_MAX_BYTES  (400 * 1024)
#define ALB_LIST_MAX_BYTES (32 * 1024)
/* reject images above this pixel count (memory protection) */
#define ALB_MAX_PIXELS     (1200000)
/* max intermediate decode buffer (JPEG downscale output) */
#define ALB_TMP_MAX        (1024 * 1024)

typedef enum {
    ALB_REQ_LIST = 0,
    ALB_REQ_IMAGE,
} alb_req_type_t;

typedef struct {
    alb_req_type_t type;
    int index;
} alb_req_t;

typedef enum {
    ALB_STATE_LOADING = 0,
    ALB_STATE_SHOW,
    ALB_STATE_EMPTY,
    ALB_STATE_ERROR,
} alb_state_t;

static char s_urls[ALB_MAX_IMAGES][ALB_URL_MAX];
static volatile int s_count;
static volatile int s_current;
static volatile alb_state_t s_state = ALB_STATE_LOADING;
static volatile bool s_list_loaded;
static char s_err[48];

static uint8_t *s_pix[2];
static int s_active_buf;
static volatile bool s_frame_ready;

static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static lv_obj_t *s_screen;
static lv_obj_t *s_main_screen;
static lv_obj_t *s_img;
static lv_obj_t *s_msg;
static lv_obj_t *s_idx;
static lv_img_dsc_t s_dsc;
static volatile bool s_open;
static bool s_inited;

/* ------------------------------------------------------------------ */
/* HTTP                                                                */
/* ------------------------------------------------------------------ */

static uint8_t *http_get(const char *url, int max_size, int *out_len)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 10000,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return NULL;
    }

    if (esp_http_client_open(client, 0) != ESP_OK) {
        ESP_LOGW(TAG, "open %s failed", url);
        esp_http_client_cleanup(client);
        return NULL;
    }

    int content_len = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGW(TAG, "GET %s -> %d", url, status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return NULL;
    }

    int cap = max_size;
    if (content_len > 0) {
        if (content_len > max_size) {
            ESP_LOGW(TAG, "%s too large: %d", url, content_len);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return NULL;
        }
        cap = content_len;
    }

    uint8_t *buf = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return NULL;
    }

    int total = 0;
    while (total < cap) {
        int r = esp_http_client_read(client, (char *)buf + total, cap - total);
        if (r <= 0) {
            break;
        }
        total += r;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (total <= 0) {
        heap_caps_free(buf);
        return NULL;
    }
    *out_len = total;
    return buf;
}

/* ------------------------------------------------------------------ */
/* Image decoding: JPEG / PNG / BMP -> 320x240 RGB565 (fill + crop)   */
/* ------------------------------------------------------------------ */

/* LVGL keeps RGB565 as big endian bytes because LV_COLOR_16_SWAP is on */
static inline void store_pixel(uint8_t *dst, int idx, uint16_t v)
{
    dst[idx * 2] = (uint8_t)(v >> 8);
    dst[idx * 2 + 1] = (uint8_t)(v & 0xFF);
}

static inline uint16_t load_pixel(const uint8_t *src, int bpp, int stride, int x, int y)
{
    if (bpp == 2) {
        return *(const uint16_t *)(src + (size_t)y * stride + (size_t)x * 2);
    }
    const uint8_t *p = src + (size_t)y * stride + (size_t)x * 3;
    return (uint16_t)(((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3));
}

/* Scale the source to fill the screen (cover), keeping aspect ratio and
 * cropping the overflow on both sides. bpp is 2 (RGB565) or 3 (RGB888). */
static void resample_fill(const uint8_t *src, int sw, int sh, int bpp, uint8_t *dst)
{
    int cw = sw;
    int ch = sh;

    if ((int64_t)sw * ALB_H > (int64_t)sh * ALB_W) {
        cw = (int)((int64_t)sh * ALB_W / ALB_H);
    } else {
        ch = (int)((int64_t)sw * ALB_H / ALB_W);
    }
    if (cw < 1) {
        cw = 1;
    }
    if (ch < 1) {
        ch = 1;
    }

    int cx = (sw - cw) / 2;
    int cy = (sh - ch) / 2;
    int stride = sw * bpp;

    for (int y = 0; y < ALB_H; y++) {
        int sy = cy + (int)((int64_t)y * ch / ALB_H);
        if (sy >= sh) {
            sy = sh - 1;
        }
        for (int x = 0; x < ALB_W; x++) {
            int sx = cx + (int)((int64_t)x * cw / ALB_W);
            if (sx >= sw) {
                sx = sw - 1;
            }
            store_pixel(dst, y * ALB_W + x, load_pixel(src, bpp, stride, sx, sy));
        }
    }
}

static bool jpeg_get_size(const uint8_t *d, int len, int *w, int *h)
{
    int i = 2; /* skip SOI */
    while (i + 9 < len) {
        if (d[i] != 0xFF) {
            i++;
            continue;
        }
        uint8_t marker = d[i + 1];
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            i += 2;
            continue;
        }
        int seg_len = (d[i + 2] << 8) | d[i + 3];
        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            *h = (d[i + 5] << 8) | d[i + 6];
            *w = (d[i + 7] << 8) | d[i + 8];
            return true;
        }
        i += 2 + seg_len;
    }
    return false;
}

static bool decode_jpeg(const uint8_t *data, int len, uint8_t *dst)
{
    int w = 0, h = 0;
    if (!jpeg_get_size(data, len, &w, &h) || w <= 0 || h <= 0) {
        ESP_LOGW(TAG, "cannot parse jpeg size");
        return false;
    }

    /* pick the smallest downscale that still covers the screen */
    int shift = 0;
    esp_jpeg_image_scale_t scale = JPEG_IMAGE_SCALE_0;
    if (w / 8 >= ALB_W && h / 8 >= ALB_H) {
        shift = 3;
        scale = JPEG_IMAGE_SCALE_1_8;
    } else if (w / 4 >= ALB_W && h / 4 >= ALB_H) {
        shift = 2;
        scale = JPEG_IMAGE_SCALE_1_4;
    } else if (w / 2 >= ALB_W && h / 2 >= ALB_H) {
        shift = 1;
        scale = JPEG_IMAGE_SCALE_1_2;
    }

    size_t tmp_bytes = (size_t)((w >> shift) + 4) * ((h >> shift) + 4) * 2;
    if (tmp_bytes > ALB_TMP_MAX) {
        ESP_LOGW(TAG, "jpeg %dx%d needs a too large buffer", w, h);
        return false;
    }

    uint8_t *tmp = heap_caps_malloc(tmp_bytes, MALLOC_CAP_SPIRAM);
    if (tmp == NULL) {
        return false;
    }

    esp_jpeg_image_cfg_t cfg = {
        .indata = (uint8_t *)data,
        .indata_size = len,
        .outbuf = tmp,
        .outbuf_size = (uint32_t)tmp_bytes,
        .out_format = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale = scale,
        .flags.swap_color_bytes = 0,
    };
    esp_jpeg_image_output_t info = {0};

    bool ok = (esp_jpeg_decode(&cfg, &info) == ESP_OK);
    if (ok) {
        resample_fill(tmp, info.width, info.height, 2, dst);
    } else {
        ESP_LOGW(TAG, "jpeg decode failed (%dx%d)", w, h);
    }
    heap_caps_free(tmp);
    return ok;
}

static bool png_get_size(const uint8_t *d, int len, int *w, int *h)
{
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (len < 24 || memcmp(d, sig, 8) != 0) {
        return false;
    }
    *w = (int)(((uint32_t)d[16] << 24) | ((uint32_t)d[17] << 16) | ((uint32_t)d[18] << 8) | d[19]);
    *h = (int)(((uint32_t)d[20] << 24) | ((uint32_t)d[21] << 16) | ((uint32_t)d[22] << 8) | d[23]);
    return true;
}

static bool decode_png(const uint8_t *data, int len, uint8_t *dst)
{
    int w = 0, h = 0;
    if (!png_get_size(data, len, &w, &h) || w <= 0 || h <= 0) {
        return false;
    }
    if ((int64_t)w * h > ALB_MAX_PIXELS) {
        ESP_LOGW(TAG, "png too large: %dx%d", w, h);
        return false;
    }

    unsigned char *rgb = NULL;
    unsigned uw = 0, uh = 0;
    unsigned err = lodepng_decode24(&rgb, &uw, &uh, data, (size_t)len);
    if (err != 0 || rgb == NULL) {
        ESP_LOGW(TAG, "png decode error %u", err);
        return false;
    }

    resample_fill(rgb, (int)uw, (int)uh, 3, dst);
    lv_mem_free(rgb);
    return true;
}

typedef struct {
    int w;
    int h;
    int bpp; /* bytes per pixel */
    bool bottom_up;
    size_t stride;
    const uint8_t *pixels;
} bmp_info_t;

static bool bmp_parse(const uint8_t *d, int len, bmp_info_t *out)
{
    if (len < 54 || d[0] != 'B' || d[1] != 'M') {
        return false;
    }
    uint32_t data_offset = (uint32_t)d[10] | ((uint32_t)d[11] << 8) | ((uint32_t)d[12] << 16) | ((uint32_t)d[13] << 24);
    uint32_t header_size = (uint32_t)d[14] | ((uint32_t)d[15] << 8) | ((uint32_t)d[16] << 16) | ((uint32_t)d[17] << 24);
    if (header_size < 40) {
        return false;
    }
    int32_t w = (int32_t)((uint32_t)d[18] | ((uint32_t)d[19] << 8) | ((uint32_t)d[20] << 16) | ((uint32_t)d[21] << 24));
    int32_t h = (int32_t)((uint32_t)d[22] | ((uint32_t)d[23] << 8) | ((uint32_t)d[24] << 16) | ((uint32_t)d[25] << 24));
    uint16_t bpp = (uint16_t)(d[28] | (d[29] << 8));
    uint32_t compression = (uint32_t)d[30] | ((uint32_t)d[31] << 8) | ((uint32_t)d[32] << 16) | ((uint32_t)d[33] << 24);
    if (w <= 0 || h == 0 || compression != 0 || (bpp != 24 && bpp != 32)) {
        return false;
    }

    int height = h > 0 ? h : -h;
    if ((int64_t)w * height > ALB_MAX_PIXELS) {
        return false;
    }

    out->w = w;
    out->h = height;
    out->bpp = bpp / 8;
    out->bottom_up = (h > 0);
    out->stride = ((size_t)w * out->bpp + 3) & ~(size_t)3;
    out->pixels = d + data_offset;

    if ((size_t)data_offset + out->stride * height > (size_t)len) {
        return false;
    }
    return true;
}

static bool decode_bmp(const uint8_t *data, int len, uint8_t *dst)
{
    bmp_info_t info;
    if (!bmp_parse(data, len, &info)) {
        return false;
    }

    size_t bytes = (size_t)info.w * info.h * 3;
    uint8_t *rgb = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (rgb == NULL) {
        return false;
    }

    for (int y = 0; y < info.h; y++) {
        int row = info.bottom_up ? (info.h - 1 - y) : y;
        const uint8_t *src_row = info.pixels + (size_t)row * info.stride;
        uint8_t *o = rgb + (size_t)y * info.w * 3;
        for (int x = 0; x < info.w; x++) {
            const uint8_t *p = src_row + (size_t)x * info.bpp;
            o[x * 3 + 0] = p[2];
            o[x * 3 + 1] = p[1];
            o[x * 3 + 2] = p[0];
        }
    }

    resample_fill(rgb, info.w, info.h, 3, dst);
    heap_caps_free(rgb);
    return true;
}

static bool decode_image(const uint8_t *data, int len, uint8_t *dst)
{
    if (len > 2 && data[0] == 0xFF && data[1] == 0xD8) {
        return decode_jpeg(data, len, dst);
    }
    if (len > 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        return decode_png(data, len, dst);
    }
    if (len > 2 && data[0] == 'B' && data[1] == 'M') {
        return decode_bmp(data, len, dst);
    }
    ESP_LOGW(TAG, "unsupported image format");
    return false;
}

/* ------------------------------------------------------------------ */
/* Worker task                                                         */
/* ------------------------------------------------------------------ */

static void load_list(void)
{
    int len = 0;
    uint8_t *buf = http_get(ALBUM_BASE_URL "/api/album/list", ALB_LIST_MAX_BYTES, &len);
    if (buf == NULL) {
        snprintf(s_err, sizeof(s_err), "列表请求失败");
        s_state = ALB_STATE_ERROR;
        return;
    }

    cJSON *root = cJSON_ParseWithLength((const char *)buf, len);
    heap_caps_free(buf);
    if (root == NULL) {
        snprintf(s_err, sizeof(s_err), "列表解析失败");
        s_state = ALB_STATE_ERROR;
        return;
    }

    int n = 0;
    const cJSON *images = cJSON_GetObjectItemCaseSensitive(root, "images");
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, images) {
        const cJSON *url = cJSON_GetObjectItemCaseSensitive(item, "url");
        if (cJSON_IsString(url) && url->valuestring != NULL && n < ALB_MAX_IMAGES) {
            strncpy(s_urls[n], url->valuestring, ALB_URL_MAX - 1);
            s_urls[n][ALB_URL_MAX - 1] = '\0';
            n++;
        }
    }
    cJSON_Delete(root);

    s_count = n;
    s_list_loaded = true;
    s_current = 0;
    ESP_LOGI(TAG, "album list: %d images", n);

    if (n == 0) {
        s_state = ALB_STATE_EMPTY;
        return;
    }

    alb_req_t req = { .type = ALB_REQ_IMAGE, .index = 0 };
    xQueueSend(s_queue, &req, 0);
}

static void load_image(int index)
{
    if (index < 0 || index >= s_count) {
        return;
    }

    char url[ALB_URL_MAX + 32];
    snprintf(url, sizeof(url), "%s%s", ALBUM_BASE_URL, s_urls[index]);

    int len = 0;
    uint8_t *jpg = http_get(url, ALB_IMG_MAX_BYTES, &len);
    if (jpg == NULL) {
        snprintf(s_err, sizeof(s_err), "下载失败");
        s_state = ALB_STATE_ERROR;
        return;
    }

    uint8_t *out = s_pix[1 - s_active_buf];
    bool ok = decode_image(jpg, len, out);
    heap_caps_free(jpg);

    if (!ok) {
        snprintf(s_err, sizeof(s_err), "解码失败");
        s_state = ALB_STATE_ERROR;
        return;
    }

    s_active_buf = 1 - s_active_buf;
    s_current = index;
    s_frame_ready = true;
    s_state = ALB_STATE_SHOW;
    ESP_LOGI(TAG, "image %d: %s (fill %dx%d)", index, s_urls[index], ALB_W, ALB_H);
}

static void album_task(void *arg)
{
    (void)arg;
    alb_req_t req;

    while (1) {
        if (xQueueReceive(s_queue, &req, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (req.type == ALB_REQ_LIST) {
            load_list();
        } else {
            load_image(req.index);
        }
    }
}

/* ------------------------------------------------------------------ */
/* LVGL UI                                                             */
/* ------------------------------------------------------------------ */

static void request_image(int index)
{
    if (s_count <= 0) {
        return;
    }
    if (s_state == ALB_STATE_LOADING) {
        return;
    }
    s_state = ALB_STATE_LOADING;
    alb_req_t req = { .type = ALB_REQ_IMAGE, .index = index };
    xQueueSend(s_queue, &req, 0);
}

static void prev_cb(lv_event_t *e)
{
    (void)e;
    if (s_count <= 0) {
        return;
    }
    request_image((s_current + s_count - 1) % s_count);
}

static void next_cb(lv_event_t *e)
{
    (void)e;
    if (s_count <= 0) {
        return;
    }
    request_image((s_current + 1) % s_count);
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    album_close();
}

static void album_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (!s_open) {
        return;
    }

    if (s_frame_ready) {
        s_frame_ready = false;
        s_dsc.header.always_zero = 0;
        s_dsc.header.reserved = 0;
        s_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
        s_dsc.header.w = ALB_W;
        s_dsc.header.h = ALB_H;
        s_dsc.data_size = ALB_PIX_BYTES;
        s_dsc.data = s_pix[s_active_buf];

        lv_img_set_src(s_img, &s_dsc);
        lv_obj_set_pos(s_img, 0, 0);
        lv_obj_clear_flag(s_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
    }

    switch (s_state) {
    case ALB_STATE_SHOW:
        lv_label_set_text_fmt(s_idx, "%d/%d", s_current + 1, (int)s_count);
        break;
    case ALB_STATE_LOADING:
        lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_msg, "加载中...");
        break;
    case ALB_STATE_EMPTY:
        lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_msg, "相册为空");
        break;
    case ALB_STATE_ERROR:
        lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(s_msg, "错误: %s", s_err);
        break;
    default:
        break;
    }
}

static lv_obj_t *make_btn(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                          const char *text)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

static void album_ui_create(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_set_style_text_font(s_screen, &lv_font_cn_16, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    s_img = lv_img_create(s_screen);
    lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);

    s_msg = lv_label_create(s_screen);
    lv_label_set_text(s_msg, "加载中...");
    lv_obj_set_style_text_color(s_msg, lv_color_hex(0xE0E0E0), 0);
    lv_obj_align(s_msg, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *bar = lv_obj_create(s_screen);
    lv_obj_set_pos(bar, 0, 198);
    lv_obj_set_size(bar, ALB_W, 42);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(0x263442), 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_90, 0);

    lv_obj_t *prev = make_btn(bar, 6, 5, 44, 32, "<");
    lv_obj_t *next = make_btn(bar, 54, 5, 44, 32, ">");
    lv_obj_t *back = make_btn(bar, 240, 5, 74, 32, "返回");
    lv_obj_add_event_cb(prev, prev_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(next, next_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);

    s_idx = lv_label_create(bar);
    lv_label_set_text(s_idx, "-/-");
    lv_obj_set_style_text_color(s_idx, lv_color_hex(0xE0E0E0), 0);
    lv_obj_align(s_idx, LV_ALIGN_CENTER, 0, 0);
}

void album_open(void)
{
    if (!s_inited) {
        s_inited = true;

        s_queue = xQueueCreate(4, sizeof(alb_req_t));
        s_pix[0] = heap_caps_malloc(ALB_PIX_BYTES, MALLOC_CAP_SPIRAM);
        s_pix[1] = heap_caps_malloc(ALB_PIX_BYTES, MALLOC_CAP_SPIRAM);
        if (s_pix[0] == NULL || s_pix[1] == NULL) {
            ESP_LOGE(TAG, "no PSRAM for album buffers");
        }
        xTaskCreate(album_task, "album", 8192, NULL, 4, &s_task);

        album_ui_create();
        lv_timer_create(album_timer_cb, 100, NULL);
    }

    if (s_main_screen == NULL) {
        s_main_screen = lv_scr_act();
    }

    if (!s_list_loaded) {
        s_state = ALB_STATE_LOADING;
        alb_req_t req = { .type = ALB_REQ_LIST, .index = 0 };
        xQueueSend(s_queue, &req, 0);
    } else if (s_state == ALB_STATE_SHOW) {
        lv_label_set_text_fmt(s_idx, "%d/%d", s_current + 1, (int)s_count);
    }

    lv_scr_load(s_screen);
    s_open = true;
}

void album_close(void)
{
    s_open = false;
    if (s_main_screen != NULL) {
        lv_scr_load(s_main_screen);
    }
}

bool album_is_open(void)
{
    return s_open;
}
