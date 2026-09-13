#include <stdio.h>
#include <stdlib.h>
#include <sys/lock.h>
#include <unistd.h>
#include "gc9a01.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "lvgl.h"

#define LVGL_TICK_PERIOD_MS     2
#define LVGL_TASK_MAX_DELAY_MS  500
#define LVGL_TASK_MIN_DELAY_MS  (1000 / CONFIG_FREERTOS_HZ)
#define LVGL_TASK_STACK_SIZE    (4*1024)
#define LVGL_TASK_PRIORITY      2
#define DRAW_BUF_LINES          20

/* GC9A01 is a round 1.28" panel - always square resolution, 240x240 */
#define GC9A01_EXAMPLE_HRES         240
#define GC9A01_EXAMPLE_VRES         240

#define GC9A01_GPIO_BL      GPIO_NUM_14
#define GC9A01_GPIO_DC      GPIO_NUM_15
#define GC9A01_GPIO_RST     GPIO_NUM_16
#define GC9A01_GPIO_CS      GPIO_NUM_10
#define GC9A01_GPIO_SCK     GPIO_NUM_17
/* GC9A01 is write-only over SPI (no MISO needed); free the pin */
#define GC9A01_GPIO_MISO    GPIO_NUM_NC
#define GC9A01_GPIO_MOSI    GPIO_NUM_11

#define GC9A01_LCD_SPI_HOST         SPI2_HOST
#define GC9A01_LCD_PIXEL_CLOCK_HZ   (40 * 1000 * 1000)
#define GC9A01_LCD_SPI_QUEUE_SIZE   1   /* polling doesn't queue, but field is required */

#define GC9A01_SPI_MAX_TRANSFER_BYTES  (GC9A01_EXAMPLE_HRES * DRAW_BUF_LINES * sizeof(uint16_t))

static const char *tag = "[GC9A01_EXAMPLE]";

static GC9A01_Hal hal;
static GC9A01_Panel panel;
static spi_device_handle_t gc9a01_spi_handle;
static lv_display_t *display = NULL;

/* Runtime cap for a single SPI transaction - hardware length-register limit
 * can be smaller than max_transfer_sz, queried below after bus init. */
static size_t gc9a01_max_trans_bytes = GC9A01_SPI_MAX_TRANSFER_BYTES;

/* ============================== HAL bindings (từ thư viện của bạn) ============================== */

static GC9A01_Status gc9a01_hal_gpio_write(GC9A01_Gpio gpio, bool level) {
    return (gpio_set_level((gpio_num_t)gpio.pin, level) == ESP_OK) ? GC9A01_OK : GC9A01_ERROR_GPIO;
}

static GC9A01_Status gc9a01_hal_gpio_reset(GC9A01_Gpio gpio) {
    return (gpio_reset_pin((gpio_num_t)gpio.pin) == ESP_OK) ? GC9A01_OK : GC9A01_ERROR_GPIO;
}

static void gc9a01_hal_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static GC9A01_Status gc9a01_hal_spi_transmit(void *ctx, const void *tx, size_t len) {
    if (len == 0) {
        return GC9A01_OK;
    }

    const uint8_t *p = (const uint8_t *)tx;
    while (len > 0) {
        size_t chunk = (len > gc9a01_max_trans_bytes) ? gc9a01_max_trans_bytes : len;

        spi_transaction_t t = {
            .length    = chunk * 8,
            .tx_buffer = p,
        };
        if (spi_device_polling_transmit((spi_device_handle_t)ctx, &t) != ESP_OK) {
            return GC9A01_ERROR_SPI;
        }

        p   += chunk;
        len -= chunk;
    }
    return GC9A01_OK;
}

static GC9A01_Status gc9a01_hal_spi_acquire_bus(void *ctx, int32_t timeout_ms) {
    TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return (spi_device_acquire_bus((spi_device_handle_t)ctx, ticks) == ESP_OK) ? GC9A01_OK : GC9A01_ERROR_SPI;
}

static GC9A01_Status gc9a01_hal_spi_release_bus(void *ctx) {
    spi_device_release_bus((spi_device_handle_t)ctx);
    return GC9A01_OK;
}

/* ============================== GPIO / SPI / HAL / bring-up ============================== */

static int gc9a01_configure_control_gpios(void) {
    gpio_config_t io_conf = {
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << GC9A01_GPIO_DC)  |
                        (1ULL << GC9A01_GPIO_RST) |
                        (1ULL << GC9A01_GPIO_CS)  |   /* CS driven manually by driver (spics_io_num = -1) */
                        (1ULL << GC9A01_GPIO_BL),
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    return (gpio_config(&io_conf) == ESP_OK) ? 0 : -1;
}

static int display_init_gc9a01_spi(void) {
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = GC9A01_GPIO_MOSI,
        .miso_io_num     = GC9A01_GPIO_MISO,
        .sclk_io_num     = GC9A01_GPIO_SCK,
        .quadwp_io_num   = GPIO_NUM_NC,
        .quadhd_io_num   = GPIO_NUM_NC,
        .max_transfer_sz = GC9A01_SPI_MAX_TRANSFER_BYTES,
    };
    if (spi_bus_initialize(GC9A01_LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO) != ESP_OK) {
        return -1;
    }

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = GC9A01_LCD_PIXEL_CLOCK_HZ,
        .mode           = 0,
        .spics_io_num   = -1, /* CS handled by the GC9A01 driver, not the SPI HW */
        .queue_size     = GC9A01_LCD_SPI_QUEUE_SIZE,
    };
    if (spi_bus_add_device(GC9A01_LCD_SPI_HOST, &dev_cfg, &gc9a01_spi_handle) != ESP_OK) {
        return -1;
    }

    size_t max_trans = 0;
    if (spi_bus_get_max_transaction_len(GC9A01_LCD_SPI_HOST, &max_trans) == ESP_OK && max_trans > 0) {
        gc9a01_max_trans_bytes = max_trans;
    }
    ESP_LOGI(tag, "gc9a01_max_trans_bytes = %u", (unsigned)gc9a01_max_trans_bytes);

    return 0;
}

static int display_init_gc9a01_hal(void) {
    GC9A01_CreateDefaultHal(&hal);

    GC9A01_Gpio dc  = { .ctx = NULL, .pin = GC9A01_GPIO_DC };
    GC9A01_Gpio rst = { .ctx = NULL, .pin = GC9A01_GPIO_RST };
    GC9A01_Gpio cs  = { .ctx = NULL, .pin = GC9A01_GPIO_CS };
    GC9A01_Gpio bkl = { .ctx = NULL, .pin = GC9A01_GPIO_BL };

    GC9A01_HalSetGpio(&hal, dc, rst, cs, bkl);
    GC9A01_HalSetGpioApis(&hal, gc9a01_hal_gpio_reset, gc9a01_hal_gpio_write);
    GC9A01_HalSetLogicLevel(&hal, /*dc_cmd*/ 0, /*dc_param*/ 1, /*cs_active*/ 0, /*rst_active*/ 0);
    GC9A01_HalSetDelayMs(&hal, gc9a01_hal_delay_ms);
    GC9A01_HalSetSpiTransMaxBytes(&hal, gc9a01_max_trans_bytes);
    GC9A01_HalSetSpiCtx(&hal, gc9a01_spi_handle);
    GC9A01_HalSetSpiTransmit(&hal, gc9a01_hal_spi_transmit);
    GC9A01_HalSetSpiAcquireBus(&hal, gc9a01_hal_spi_acquire_bus);
    GC9A01_HalSetSpiReleaseBus(&hal, gc9a01_hal_spi_release_bus);

    /* Must be `static`: GC9A01_CreatePanel() stores a raw pointer to this
     * struct in panel->config - a stack local would dangle after return. */
    static GC9A01_Config config = {
        .bits_per_pixel    = 16,
        .x_gap             = 0,
        .y_gap             = 0,
        .data_endian       = GC9A01_RGB_DATA_ENDIAN_LITTLE,
        .rgb_element_order = GC9A01_RGB_ELEMENT_ORDER_RGB,
    };

    return (GC9A01_CreatePanel(&panel, &hal, &config, NULL) == GC9A01_OK) ? 0 : -1;
}

static int display_init_gc9a01_sequence(void) {
    if (GC9A01_Reset(&panel) != GC9A01_OK)                { ESP_LOGE(tag, "Reset failed");    return -1; }
    if (GC9A01_Init(&panel) != GC9A01_OK)                 { ESP_LOGE(tag, "Init failed");     return -1; }
    if (GC9A01_DispSleep(&panel, false) != GC9A01_OK)     { ESP_LOGE(tag, "Wake failed");     return -1; } /* SLPOUT - bắt buộc, Init() chỉ gửi SLPIN */
    if (GC9A01_InvertColor(&panel, true) != GC9A01_OK)    { ESP_LOGE(tag, "InvertColor failed");return -1; }
    if (GC9A01_Mirror(&panel, true, false) != GC9A01_OK)  { ESP_LOGE(tag, "Mirror failed");   return -1; }
    if (GC9A01_DispOnOff(&panel, true) != GC9A01_OK)      { ESP_LOGE(tag, "DispOnOff failed");return -1; }
    if (GC9A01_BacklightOnOff(&panel, true) != GC9A01_OK) { ESP_LOGE(tag, "Backlight failed");return -1; }
    return 0;
}

static int8_t display_init_lcd(void){
    if (gc9a01_configure_control_gpios() != 0) { ESP_LOGE(tag, "gpio config failed"); return -1; }
    if (display_init_gc9a01_spi() != 0)        { ESP_LOGE(tag, "spi init failed");    return -1; }
    if (display_init_gc9a01_hal() != 0)        { ESP_LOGE(tag, "hal/panel init failed"); return -1; }
    if (display_init_gc9a01_sequence() != 0)   { ESP_LOGE(tag, "bring-up sequence failed"); return -1; }
    return 0;
}

/* ============================== LVGL glue ============================== */

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map){
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;

    /* GC9A01_DrawBitmap dùng spi_device_polling_transmit bên trong -> đồng bộ,
     * trả về khi đã truyền xong thật trên bus. Khác với esp_lcd_panel_draw_bitmap
     * (bất đồng bộ, cần callback on_color_trans_done), nên ở đây gọi
     * lv_display_flush_ready() ngay tại chỗ, không cần đăng ký callback riêng. */
    GC9A01_Status st = GC9A01_DrawBitmap(&panel, offsetx1, offsety1,
                                          offsetx2 + 1, offsety2 + 1, (uint16_t *)px_map);
    if (st != GC9A01_OK) {
        ESP_LOGE(tag, "DrawBitmap failed: %d", (int)st);
    }
    lv_display_flush_ready(disp);
}

static void lvgl_increase_tick(void *args){
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
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

/* ---- Text demo ---- */

static void lvgl_demo_text_ui(lv_display_t *disp)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(label, "Hello Espressif, Hello LVGL.");
    lv_obj_set_width(label, lv_display_get_horizontal_resolution(disp));
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);
}

/* ---- Digital clock demo ---- */

static lv_obj_t *time_label = NULL;

/* Fake time source for now: increments once per second from 00:00:00.
 * Swap this struct + clock_tick_cb body for a real RTC/NTP read later -
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

/* ---- Toggle switch demo ---- */

static void toggle_switch_event_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool is_on = lv_obj_has_state(sw, LV_STATE_CHECKED);

    lv_obj_t *scr = lv_obj_get_parent(sw);
    lv_obj_set_style_bg_color(scr, is_on ? lv_color_white() : lv_color_black(), 0);

    lv_obj_t *status_label = (lv_obj_t *)lv_event_get_user_data(e);
    lv_label_set_text(status_label, is_on ? "ON" : "OFF");
    lv_obj_set_style_text_color(status_label, is_on ? lv_color_black() : lv_color_white(), 0);
}

static void lvgl_demo_toggle_switch_ui(lv_display_t *disp)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *status_label = lv_label_create(scr);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(status_label, lv_color_white(), 0);
    lv_label_set_text(status_label, "OFF");
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *sw = lv_switch_create(scr);
    lv_obj_align(sw, LV_ALIGN_CENTER, 0, 30);
    /* status_label passed as user_data so the callback can update it directly,
     * avoiding a second static global like time_label above */
    lv_obj_add_event_cb(sw, toggle_switch_event_cb, LV_EVENT_VALUE_CHANGED, status_label);
}

/* ---- Color cycle demo (LVGL version of the raw fill_solid test loop) ---- */

static lv_obj_t *color_rect = NULL;

static const lv_color_t color_cycle[] = {
    LV_COLOR_MAKE(0xFF, 0x00, 0x00), /* red */
    LV_COLOR_MAKE(0x00, 0xFF, 0x00), /* green */
    LV_COLOR_MAKE(0x00, 0x00, 0xFF), /* blue */
    LV_COLOR_MAKE(0xFF, 0xFF, 0xFF), /* white */
};
#define COLOR_CYCLE_COUNT (sizeof(color_cycle) / sizeof(color_cycle[0]))

static void color_cycle_tick_cb(lv_timer_t *timer)
{
    static uint8_t idx = 0;
    lv_obj_set_style_bg_color(color_rect, color_cycle[idx], 0);
    idx++;
    if (idx == COLOR_CYCLE_COUNT) { idx = 0; }
}

static void lvgl_demo_color_toggle_ui(lv_display_t *disp)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);

    color_rect = lv_obj_create(scr);
    lv_obj_remove_style_all(color_rect); /* strip default border/padding/scrollbar */
    lv_obj_set_size(color_rect, lv_display_get_horizontal_resolution(disp),
                                 lv_display_get_vertical_resolution(disp));
    lv_obj_set_pos(color_rect, 0, 0);
    lv_obj_set_style_bg_opa(color_rect, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(color_rect, color_cycle[0], 0);

    /* 500ms period, matches vTaskDelay(pdMS_TO_TICKS(500)) in the original raw-driver loop */
    lv_timer_create(color_cycle_tick_cb, 500, NULL);
}

/* ============================== LVGL init ============================== */

static int8_t display_init_lvgl(void){
    lv_init();
    display = lv_display_create(GC9A01_EXAMPLE_HRES, GC9A01_EXAMPLE_VRES);

    size_t draw_buffer_sz = GC9A01_EXAMPLE_HRES * DRAW_BUF_LINES * sizeof(lv_color16_t);

    void *buf1 = spi_bus_dma_memory_alloc(GC9A01_LCD_SPI_HOST, draw_buffer_sz, 0);
    if(buf1 == NULL) { return -1; }
    void *buf2 = spi_bus_dma_memory_alloc(GC9A01_LCD_SPI_HOST, draw_buffer_sz, 0);
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

    /* Không cần esp_lcd_panel_io_register_event_callbacks: driver của này vẽ
     * đồng bộ (polling transmit) nên flush_ready đã được gọi trực tiếp trong
     * lvgl_flush_cb, không cần callback bất đồng bộ. */

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
    /* Chọn 1 trong các demo bằng cách đổi dòng gọi dưới đây:
     *   lvgl_demo_text_ui(display);
     *   lvgl_demo_clock_ui(display);
     *   lvgl_demo_toggle_switch_ui(display);
     *   lvgl_demo_color_toggle_ui(display);
     */
    lvgl_demo_color_toggle_ui(display);
    _lock_release(&lvgl_api_lock);
}