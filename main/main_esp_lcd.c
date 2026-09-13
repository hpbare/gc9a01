/* Example này sẽ đi từ chi tiết tới tổng quan hóa thông qua việc module hóa từ từ */
#include <stdio.h>
#include <stdlib.h>              /* for free() */
#include <sys/lock.h>            /* Wrapper FreeRTOS Semaphore */
#include <unistd.h>
#include "esp_lcd_gc9a01.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"

#define LVGL_TICK_PERIOD_MS     2
#define LVGL_TASK_MAX_DELAY_MS  500
#define LVGL_TASK_MIN_DELAY_MS  (1000 / CONFIG_FREERTOS_HZ)
#define LVGL_TASK_STACK_SIZE    (4*1024)
#define LVGL_TASK_PRIORITY      2
#define DRAW_BUF_LINES          20 // number of display lines in each draw buffer

#define GC9A01_CMD_BITS             8
#define GC9A01_PARAM_BITS           8
#define GC9A01_BK_LIGHT_ON_LEVEL    1

/* GC9A01 is a round 1.28" panel — always square resolution, 240x240 */
#define GC9A01_EXAMPLE_HRES         240
#define GC9A01_EXAMPLE_VRES         240

#define GC9A01_EXAMPLE_GPIO_BL      GPIO_NUM_14
#define GC9A01_EXAMPLE_GPIO_DC      GPIO_NUM_15
#define GC9A01_EXAMPLE_GPIO_RST     GPIO_NUM_16
#define GC9A01_EXAMPLE_GPIO_CS      GPIO_NUM_10
#define GC9A01_EXAMPLE_GPIO_SCK     GPIO_NUM_17
/* GC9A01 is write-only over SPI (no MISO needed); free the pin */
#define GC9A01_EXAMPLE_GPIO_MISO    GPIO_NUM_NC
#define GC9A01_EXAMPLE_GPIO_MOSI    GPIO_NUM_11

#define GC9A01_EXAMPLE_LCD_SPI_HOST SPI2_HOST
#define GC9A01_EXAMPLE_LCD_PIXEL_CLOCK_HZ     (20 * 1000 * 1000)

static const char *tag = "[GC9A01_EXAMPLE]";
static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;
static lv_display_t *display = NULL;


/** @brief Init SPI bus, LCD GPIOs, IO layer, panel layer, start LCD with sequence */
static int8_t display_init_lcd(void){
    /* Hardware Init */
    gpio_config_t backlight_gpio_config = {
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << GC9A01_EXAMPLE_GPIO_BL,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    if(gpio_config(&backlight_gpio_config) != ESP_OK){
        return -1;
    }

    /* SPI Init - Chỗ này khởi tạo SPI bus*/
    spi_bus_config_t buscfg = {
        .mosi_io_num = GC9A01_EXAMPLE_GPIO_MOSI,
        .miso_io_num = GC9A01_EXAMPLE_GPIO_MISO,
        .sclk_io_num = GC9A01_EXAMPLE_GPIO_SCK,
        .quadhd_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .max_transfer_sz = GC9A01_EXAMPLE_HRES * DRAW_BUF_LINES * sizeof(uint16_t)
    };
    if(spi_bus_initialize(GC9A01_EXAMPLE_LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO) != ESP_OK){
        return -1;
    }

    /* Panel IO layer init - Chỗ này khởi tạo lớp IO trong kiến trúc esp_lcd của ESP-IDF */
    esp_lcd_panel_io_spi_config_t iocfg = {
        .dc_gpio_num       = GC9A01_EXAMPLE_GPIO_DC,
        .cs_gpio_num       = GC9A01_EXAMPLE_GPIO_CS,
        .pclk_hz           = GC9A01_EXAMPLE_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits      = GC9A01_CMD_BITS,
        .lcd_param_bits    = GC9A01_PARAM_BITS,
        .spi_mode          = 0,
        .trans_queue_depth = 10
    };
    if(esp_lcd_new_panel_io_spi(GC9A01_EXAMPLE_LCD_SPI_HOST, &iocfg, &io_handle) != ESP_OK){
        return -1;
    }

    /* Panel layer init - Chỗ này khởi tạo lớp panel (kiến trúc esp_lcd của ESP-IDF) */
    esp_lcd_panel_dev_config_t panelcfg = {
        .reset_gpio_num = GC9A01_EXAMPLE_GPIO_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16
    };
    if(esp_lcd_new_panel_gc9a01(io_handle, &panelcfg, &panel_handle) != ESP_OK){
        return -1;
    }

    /* Start sequence */
    if(esp_lcd_panel_reset(panel_handle) != ESP_OK)  { return -1; }
    if(esp_lcd_panel_init(panel_handle) != ESP_OK)   { return -1; }
    if(esp_lcd_panel_mirror(panel_handle, true, false) != ESP_OK) { return -1; }
    if(esp_lcd_panel_disp_on_off(panel_handle, true) != ESP_OK) { return -1; }
    if(gpio_set_level(GC9A01_EXAMPLE_GPIO_BL, GC9A01_BK_LIGHT_ON_LEVEL) != ESP_OK) { return -1; }

    return 0;
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map){
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;

    lv_draw_sw_rgb565_swap(px_map, (offsetx2 + 1 - offsetx1) * (offsety2 + 1 - offsety1));
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);
}

static void lvgl_increase_tick(void *args){
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static bool lvgl_notify_flush_ready(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *userctx){
    lv_display_t *disp = (lv_display_t *)userctx;
    lv_display_flush_ready(disp);
    return false;
}

static _lock_t lvgl_api_lock;
static void lvgl_port_task(void *arg)
{
    ESP_LOGI(tag, "Starting LVGL task");
    uint32_t time_till_next_ms = 0;
    while (1) {
        _lock_acquire(&lvgl_api_lock);
        time_till_next_ms = lv_timer_handler();
        _lock_release(&lvgl_api_lock);
        if(time_till_next_ms < LVGL_TASK_MIN_DELAY_MS) { time_till_next_ms = LVGL_TASK_MIN_DELAY_MS; }
        if(time_till_next_ms > LVGL_TASK_MAX_DELAY_MS) { time_till_next_ms = LVGL_TASK_MAX_DELAY_MS; }
        usleep(1000 * time_till_next_ms);
    }
}

/* ---- Digital clock UI ---- */

static lv_obj_t *time_label = NULL;

/* Fake time source for now: increments once per second from 00:00:00.
 * Swap this struct + clock_tick_cb body for a real RTC/NTP read later —
 * the label update / redraw path below stays the same. */
static struct {
    uint8_t h, m, s;
} fake_time = { 0, 0, 0 };

static void clock_tick_cb(lv_timer_t *timer)
{
    fake_time.s++;
    if (fake_time.s >= 60) { fake_time.s = 0; fake_time.m++; }
    if (fake_time.m >= 60) { fake_time.m = 0; fake_time.h++; }
    if (fake_time.h >= 24) { fake_time.h = 0; }

    char buf[16]; /* "HH:MM:SS\0" */
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", fake_time.h, fake_time.m, fake_time.s);
    lv_label_set_text(time_label, buf);
}

static void lvgl_demo_text_ui(lv_display_t *disp)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(label, "Hello Espressif, Hello LVGL.");
    lv_obj_set_width(label, lv_display_get_horizontal_resolution(disp));
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);
}

static void lvgl_demo_clock_ui(lv_display_t *disp)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    time_label = lv_label_create(scr);
    /* Montserrat 36 must be enabled: menuconfig -> Component config ->
     * LVGL -> Font usage -> Enable Montserrat 36 (CONFIG_LV_FONT_MONTSERRAT_36) */
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(time_label, lv_color_white(), 0);
    lv_label_set_text(time_label, "00:00:00");
    lv_obj_center(time_label); /* horizontal row through screen center is the widest chord on a round panel */

    /* 1-second fake tick. Runs inside lv_timer_handler(), which lvgl_port_task
     * already calls under lvgl_api_lock, so no extra locking is needed here. */
    lv_timer_create(clock_tick_cb, 1000, NULL);
}

static int8_t display_init_lvgl(void){
    lv_init();
    display = lv_display_create(GC9A01_EXAMPLE_HRES, GC9A01_EXAMPLE_VRES);

    size_t draw_buffer_sz = GC9A01_EXAMPLE_HRES * DRAW_BUF_LINES * sizeof(lv_color16_t);

    void *buf1 = spi_bus_dma_memory_alloc(GC9A01_EXAMPLE_LCD_SPI_HOST, draw_buffer_sz, 0);
    if(buf1 == NULL) { return -1; }
    void *buf2 = spi_bus_dma_memory_alloc(GC9A01_EXAMPLE_LCD_SPI_HOST, draw_buffer_sz, 0);
    if(buf2 == NULL) {
        free(buf1);
        return -1;
    }

    lv_display_set_buffers(display, buf1, buf2, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, lvgl_flush_cb);

    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = lvgl_increase_tick,
        .name     = "lvgl_tick",
        .arg      = NULL
    };
    esp_timer_handle_t lvgl_tick_timer_handle = NULL;
    if(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer_handle) != ESP_OK){
        free(buf1);
        free(buf2);
        return -1;
    }
    if(esp_timer_start_periodic(lvgl_tick_timer_handle, LVGL_TICK_PERIOD_MS * 1000) != ESP_OK){
        free(buf1);
        free(buf2);
        return -1;
    }

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = lvgl_notify_flush_ready
    };
    if(esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, display) != ESP_OK){
        free(buf1);
        free(buf2);
        return -1;
    }

    return 0;
}

void app_main(void)
{
    if(display_init_lcd() != 0){
        ESP_LOGE(tag, "LCD init failed");
        return;
    }
    if(display_init_lvgl() != 0){
        ESP_LOGE(tag, "LVGL init failed");
        return;
    }

    xTaskCreate(lvgl_port_task, "LVGL", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL);

    _lock_acquire(&lvgl_api_lock);
    lvgl_demo_clock_ui(display);
    _lock_release(&lvgl_api_lock);
}