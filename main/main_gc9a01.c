#include "gc9a01.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "app_main";

/* ======================== Pin & bus configuration ======================== */

#define GC9A01_LCD_HRES   240
#define GC9A01_LCD_VRES   240

#define GC9A01_GPIO_BL      GPIO_NUM_14
#define GC9A01_GPIO_DC      GPIO_NUM_15
#define GC9A01_GPIO_RST     GPIO_NUM_16
#define GC9A01_GPIO_CS      GPIO_NUM_10
#define GC9A01_GPIO_SCK     GPIO_NUM_17
#define GC9A01_GPIO_MISO    GPIO_NUM_18
#define GC9A01_GPIO_MOSI    GPIO_NUM_11

#define GC9A01_LCD_SPI_HOST         SPI2_HOST
#define GC9A01_LCD_PIXEL_CLOCK_HZ   (1 * 1000 * 1000)
#define GC9A01_LCD_SPI_QUEUE_SIZE   10

#define GC9A01_SPI_MAX_TRANSFER_BYTES  (GC9A01_LCD_HRES * 80 * sizeof(uint16_t))

static GC9A01_Hal hal;
static GC9A01_Panel panel;
static spi_device_handle_t gc9a01_spi_handle;

/* Runtime cap for a single SPI transaction — hardware length-register limit
 * can be smaller than max_transfer_sz, queried below after bus init. */
static size_t gc9a01_max_trans_bytes = GC9A01_SPI_MAX_TRANSFER_BYTES;

/* ============================== HAL bindings ============================== */

static GC9A01_Status gc9a01_hal_gpio_write(GC9A01_Gpio gpio, bool level) {
    ESP_LOGI("GC9A01_HAL", "gpio_write pin=%d level=%d", (int)gpio.pin, (int)level);
    return (gpio_set_level((gpio_num_t)gpio.pin, level) == ESP_OK) ? GC9A01_OK : GC9A01_ERROR_GPIO;
}

static GC9A01_Status gc9a01_hal_gpio_reset(GC9A01_Gpio gpio) {
    return (gpio_reset_pin((gpio_num_t)gpio.pin) == ESP_OK) ? GC9A01_OK : GC9A01_ERROR_GPIO;
}

static void gc9a01_hal_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static GC9A01_Status gc9a01_hal_spi_transmit(void *ctx, const void *tx, size_t len) {
    ESP_LOGW("GC9A01_HAL", "transmit len=%u", (unsigned)len);
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

/* ============================== GPIO setup ============================== */

static int gc9a01_configure_control_gpios(void) {
    gpio_config_t io_conf = {
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << GC9A01_GPIO_DC) | (1ULL << GC9A01_GPIO_RST) | (1ULL << GC9A01_GPIO_BL),
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    return (gpio_config(&io_conf) == ESP_OK) ? 0 : -1;
}

/* ============================== SPI bus setup ============================== */

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
    ESP_LOGI(TAG, "gc9a01_max_trans_bytes = %u", (unsigned)gc9a01_max_trans_bytes);

    return 0;
}

/* ============================== HAL setup ============================== */

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
    GC9A01_HalSetTransmitType(&hal, GC9A01_SPI_TRANSMIT_TYPE_POLLING);
    GC9A01_HalSetSpiTransMaxBytes(&hal, gc9a01_max_trans_bytes);
    GC9A01_HalSetSpiCtx(&hal, gc9a01_spi_handle);
    GC9A01_HalSetSpiTransmit(&hal, gc9a01_hal_spi_transmit);
    GC9A01_HalSetSpiAcquireBus(&hal, gc9a01_hal_spi_acquire_bus);
    GC9A01_HalSetSpiReleaseBus(&hal, gc9a01_hal_spi_release_bus);

    /* Must be `static`: GC9A01_CreatePanel() stores a raw pointer to this
     * struct in panel->config and writes fb_bits_per_pixels into it *after*
     * this function returns (via later GC9A01_DrawBitmap calls) — a stack
     * local here would be a dangling pointer the moment we return. */
    static GC9A01_Config config = {
        .bits_per_pixel    = 16,
        .x_gap             = 0,
        .y_gap             = 0,
        .data_endian       = GC9A01_RGB_DATA_ENDIAN_BIG,
        .rgb_element_order = GC9A01_RGB_ELEMENT_ORDER_BGR,
    };

    return (GC9A01_CreatePanel(&panel, &hal, &config, NULL) == GC9A01_OK) ? 0 : -1;
}

/* ============================== Panel bring-up ============================== */

static int display_init_gc9a01_sequence(void) {
    if (GC9A01_Reset(&panel) != GC9A01_OK)                { ESP_LOGE(TAG, "Reset failed");    return -1; }
    if (GC9A01_Init(&panel) != GC9A01_OK)                 { ESP_LOGE(TAG, "Init failed");     return -1; }
    if (GC9A01_DispSleep(&panel, false) != GC9A01_OK)     { ESP_LOGE(TAG, "Wake failed");     return -1; } /* SLPOUT — bắt buộc, GC9A01_Init() chỉ gửi SLPIN */
    if (GC9A01_Mirror(&panel, true, false) != GC9A01_OK)  { ESP_LOGE(TAG, "Mirror failed");   return -1; }
    if (GC9A01_DispOnOff(&panel, true) != GC9A01_OK)      { ESP_LOGE(TAG, "DispOnOff failed");return -1; }
    if (GC9A01_BacklightOnOff(&panel, true) != GC9A01_OK) { ESP_LOGE(TAG, "Backlight failed");return -1; }
    return 0;
}
static int display_init_gc9a01(void) {
    if (gc9a01_configure_control_gpios() != 0) { ESP_LOGE(TAG, "gpio config failed"); return -1; }
    if (display_init_gc9a01_spi() != 0)        { ESP_LOGE(TAG, "spi init failed");    return -1; }
    if (display_init_gc9a01_hal() != 0)        { ESP_LOGE(TAG, "hal/panel init failed"); return -1; }
    if (display_init_gc9a01_sequence() != 0)   { ESP_LOGE(TAG, "bring-up sequence failed"); return -1; }
    return 0;
}





/* =========================================== LVGL =============================== */
#include "lvgl.h"
#include "esp_timer.h"
#define GC9A01_DRAW_BUFFER_LINE 20
#define LVGL_TICK_PERIOD_MS 2
static lv_display_t *display = NULL;


static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;

    uint32_t px_map_size = (offsetx2 + 1 - offsetx1) * (offsety2 + 1 - offsety1);
    lv_draw_sw_rgb565_swap(px_map, px_map_size);
    GC9A01_Status s = GC9A01_DrawBitmap(&panel, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);
    return s;
}

static void lvgl_increase_tick(void *args) {
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}



static int8_t display_init_lvgl(void) {
    lv_init();
    display = lv_display_create(GC9A01_LCD_HRES, GC9A01_LCD_VRES);

    size_t draw_buffer_size = GC9A01_LCD_HRES * GC9A01_DRAW_BUFFER_LINE * sizeof(lv_color16_t);

    void *buf1 = spi_bus_dma_memory_alloc(GC9A01_LCD_SPI_HOST, draw_buffer_size, 0);
    if(buf1 == NULL) { return -1; }
    void *buf2 = spi_bus_dma_memory_alloc(GC9A01_LCD_SPI_HOST, draw_buffer_size, 0);
    if(buf2 == NULL) {
        free(buf1);
        return -1;
    }

    lv_display_set_buffers(display, buf1, buf2, draw_buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, lvgl_flush_cb);

    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = lvgl_increase_tick,
        .name     = "lvgl_tick",
        .arg      = NULL
    };
    esp_timer_handle_t lvgl_tick_timer_handle = NULL;

    if(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer_handle) != ESP_OK) {
        free(buf1);
        free(buf2);
        return -1;
    }

    if(esp_timer_start_periodic(lvgl_tick_timer_handle, LVGL_TICK_PERIOD_MS * 1000) != ESP_OK) {
        free(buf1);
        free(buf2);
        return -1;
    }

    


    return -1;
}














/* ============================== Test pattern ============================== */
#define TEST_W GC9A01_LCD_HRES
#define TEST_H GC9A01_LCD_VRES
static uint8_t test_buf[TEST_W * TEST_H * 2];

void app_main(void) {
    if (display_init_gc9a01() != 0) {
        ESP_LOGE(TAG, "display_init_gc9a01 failed");
        return;
    }

    for (int y = 0; y < TEST_H; y++) {
        uint8_t hi, lo;
        if (y < TEST_H / 3) {
            hi = 0xF8; lo = 0x00; /* dải 1 (trên): đỏ thuần   0xF800 */
        } else if (y < 2 * TEST_H / 3) {
            hi = 0x07; lo = 0xE0; /* dải 2 (giữa): xanh lá thuần 0x07E0 */
        } else {
            hi = 0x00; lo = 0x1F; /* dải 3 (dưới): xanh dương thuần 0x001F */
        }
        for (int x = 0; x < TEST_W; x++) {
            int idx = (y * TEST_W + x) * 2;
            test_buf[idx + 0] = hi;
            test_buf[idx + 1] = lo;
        }
    }

    GC9A01_Status st = GC9A01_DrawBitmap(&panel, 0, 0, TEST_W, TEST_H, test_buf);
    ESP_LOGI(TAG, "DrawBitmap returned %d", (int)st);
}