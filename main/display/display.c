#include "display.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

#define GC9A01_LCD_HRES             240
#define GC9A01_LCD_VRES             240
#define GC9A01_LCD_PIXEL_CLOCK_HZ   (40 * 1000 * 1000)
#define GC9A01_SPI_MAX_TRANS_BYTES  (GC9A01_LCD_HRES * LVGL_DRAW_BUFFER_LINES * sizeof(uint16_t))

static const char *TAG = "[DISPLAY]";

/* ---------------------------------------------------------------------------- */
/* ------------------------------ DISPLAY GC9A01 ------------------------------ */
/* ---------------------------------------------------------------------------- */

static spi_device_handle_t gc9a01_spi_device_handle = NULL;
static size_t gc9a01_max_trans_bytes = GC9A01_SPI_MAX_TRANS_BYTES;
static GC9A01_Panel panel;

/** @brief Configure control GPIOs (DC, RST, CS, backlight). */
static int8_t display_init_gpio(void) {
    gpio_config_t io_cfg = {
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << GC9A01_GPIO_DC) | (1ULL << GC9A01_GPIO_RST) |
                         (1ULL << GC9A01_GPIO_CS) | (1ULL << GC9A01_GPIO_BL),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    if (gpio_config(&io_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIOs.");
        return -1;
    }
    return 0;
}

/** @brief Configure and init the SPI bus/device used by the panel. */
static int8_t display_init_spi(void) {
    spi_bus_config_t spi_bus_cfg = {
        .mosi_io_num     = GC9A01_GPIO_MOSI,
        .miso_io_num     = GC9A01_GPIO_MISO,
        .sclk_io_num     = GC9A01_GPIO_SCK,
        .quadhd_io_num   = -1,
        .quadwp_io_num   = -1,
        .max_transfer_sz = GC9A01_SPI_MAX_TRANS_BYTES
    };
    if (spi_bus_initialize(SPI2_HOST, &spi_bus_cfg, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus.");
        return -1;
    }

    spi_device_interface_config_t spi_dev_cfg = {
        .clock_speed_hz = GC9A01_LCD_PIXEL_CLOCK_HZ,
        .mode           = 0,
        .spics_io_num   = -1,  /* CS is driven manually via GC9A01_GPIO_CS */
        .queue_size     = 1    /* blocking transfers only, no queueing */
    };
    if (spi_bus_add_device(SPI2_HOST, &spi_dev_cfg, &gc9a01_spi_device_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device.");
        return -1;
    }

    size_t max_trans = 0;
    if (spi_bus_get_max_transaction_len(SPI2_HOST, &max_trans) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to query max SPI transaction length.");
        return -1;
    }
    gc9a01_max_trans_bytes = max_trans;

    return 0;
}

/** @brief HAL GPIO reset function. */
static GC9A01_Status gc9a01_gpio_reset(GC9A01_Gpio gpio) {
    return (gpio_reset_pin((gpio_num_t)gpio.pin) == ESP_OK) ? GC9A01_OK : GC9A01_ERROR_INVALID_ARGS;
}

/** @brief HAL GPIO set level function. */
static GC9A01_Status gc9a01_gpio_write(GC9A01_Gpio gpio, bool level) {
    return (gpio_set_level((gpio_num_t)gpio.pin, (uint32_t)level) == ESP_OK) ? GC9A01_OK : GC9A01_ERROR_INVALID_ARGS;
}

/** @brief HAL delay in milliseconds. */
static void gc9a01_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/** @brief HAL blocking SPI transmit, chunked to the bus's max transaction size. */
static GC9A01_Status gc9a01_spi_transmit(void *ctx, const void *tx, size_t len) {
    if (len == 0) {
        return GC9A01_OK;
    }

    const uint8_t *p = (const uint8_t *)tx;
    while (len > 0) {
        size_t chunk = (len > gc9a01_max_trans_bytes) ? gc9a01_max_trans_bytes : len;
        spi_transaction_t t = {
            .length    = chunk * 8,
            .tx_buffer = p
        };
        if (spi_device_polling_transmit((spi_device_handle_t)ctx, &t) != ESP_OK) {
            return GC9A01_ERROR_SPI;
        }
        p   += chunk;
        len -= chunk;
    }
    return GC9A01_OK;
}

/** @brief HAL acquire SPI bus function. */
static GC9A01_Status gc9a01_spi_acquire_bus(void *ctx, int32_t timeout_ms) {
    TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return (spi_device_acquire_bus((spi_device_handle_t)ctx, ticks) == ESP_OK) ? GC9A01_OK : GC9A01_ERROR_SPI;
}

/** @brief HAL release SPI bus function. */
static GC9A01_Status gc9a01_spi_release_bus(void *ctx) {
    spi_device_release_bus((spi_device_handle_t)ctx);
    return GC9A01_OK;
}

/** @brief Build the HAL, create the panel, and run the GC9A01 init sequence. */
static int8_t display_init_gc9a01(void) {
    static GC9A01_Hal hal;
    GC9A01_Gpio dc  = { .ctx = NULL, .pin = GC9A01_GPIO_DC };
    GC9A01_Gpio rst = { .ctx = NULL, .pin = GC9A01_GPIO_RST };
    GC9A01_Gpio cs  = { .ctx = NULL, .pin = GC9A01_GPIO_CS };
    GC9A01_Gpio bkl = { .ctx = NULL, .pin = GC9A01_GPIO_BL };

    GC9A01_CreateDefaultHal(&hal);
    GC9A01_HalSetGpio(&hal, dc, rst, cs, bkl);
    GC9A01_HalSetGpioApis(&hal, gc9a01_gpio_reset, gc9a01_gpio_write);
    GC9A01_HalSetLogicLevel(&hal, 0, 1, 0, 0, 1);
    GC9A01_HalSetDelayMs(&hal, gc9a01_delay_ms);
    GC9A01_HalSetSpiTransMaxBytes(&hal, gc9a01_max_trans_bytes);
    GC9A01_HalSetSpiCtx(&hal, gc9a01_spi_device_handle); /* passed to SpiTransmit/Acquire/Release */
    GC9A01_HalSetSpiTransmit(&hal, gc9a01_spi_transmit);
    GC9A01_HalSetSpiAcquireBus(&hal, gc9a01_spi_acquire_bus);
    GC9A01_HalSetSpiReleaseBus(&hal, gc9a01_spi_release_bus);

    static GC9A01_Config config = {
        .bits_per_pixel    = 16,
        .x_gap             = 0,
        .y_gap             = 0,
        .data_endian       = GC9A01_RGB_DATA_ENDIAN_LITTLE,
        .rgb_element_order = GC9A01_RGB_ELEMENT_ORDER_RGB
    };

    if (GC9A01_CreatePanel(&panel, &hal, &config, NULL) != GC9A01_OK) {
        ESP_LOGE(TAG, "Failed to create GC9A01 panel.");
        return -1;
    }

    GC9A01_Reset(&panel);
    GC9A01_Init(&panel);
    GC9A01_DispSleep(&panel, false);
    GC9A01_InvertColor(&panel, true);
    GC9A01_Mirror(&panel, true, false);
    GC9A01_DispOnOff(&panel, true);
    GC9A01_BacklightOnOff(&panel, true);

    return 0;
}

/** @brief GPIO + SPI + GC9A01 driver bring-up, in order. */
static int8_t display_init_lcd(void) {
    if (display_init_gpio() != 0) {
        return -1;
    }
    if (display_init_spi() != 0) {
        return -1;
    }
    if (display_init_gc9a01() != 0) {
        return -1;
    }
    return 0;
}

/* ---------------------------------------------------------------------------- */
/* ------------------------------- DISPLAY LVGL ------------------------------- */
/* ---------------------------------------------------------------------------- */

_lock_t lvgl_api_lock;   /* shared with display_demo.c via display_internal.h */
lv_display_t *display = NULL;

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    GC9A01_DrawBitmap(&panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, (uint16_t *)px_map);
    lv_display_flush_ready(disp);
}

static void lvgl_increase_tick(void *arg) {
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

/** @brief Create the LVGL display, draw buffers, flush cb, and tick timer. */
static int8_t display_init_lvgl(void) {
    lv_init();
    display = lv_display_create(GC9A01_LCD_HRES, GC9A01_LCD_VRES);
    size_t draw_buffer_size = GC9A01_LCD_HRES * LVGL_DRAW_BUFFER_LINES * sizeof(lv_color16_t);

    void *buf1 = spi_bus_dma_memory_alloc(SPI2_HOST, draw_buffer_size, 0);
    if (buf1 == NULL) {
        ESP_LOGE(TAG, "Failed to allocate draw buffer 1.");
        return -1;
    }
    void *buf2 = spi_bus_dma_memory_alloc(SPI2_HOST, draw_buffer_size, 0);
    if (buf2 == NULL) {
        ESP_LOGE(TAG, "Failed to allocate draw buffer 2.");
        free(buf1);
        return -1;
    }

    lv_display_set_buffers(display, buf1, buf2, draw_buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, lvgl_flush_cb);

    const esp_timer_create_args_t tick_timer_args = {
        .callback = lvgl_increase_tick,
        .name     = "lvgl_tick",
        .arg      = NULL
    };
    esp_timer_handle_t tick_timer = NULL;
    if (esp_timer_create(&tick_timer_args, &tick_timer) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LVGL tick timer.");
        free(buf1);
        free(buf2);
        return -1;
    }
    if (esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start LVGL tick timer.");
        esp_timer_delete(tick_timer);
        free(buf1);
        free(buf2);
        return -1;
    }

    return 0;
}

void lvgl_port_task(void *arg) {
    ESP_LOGI(TAG, "Starting LVGL task.");
    while (1) {
        _lock_acquire(&lvgl_api_lock);
        uint32_t time_till_next_ms = lv_timer_handler();
        _lock_release(&lvgl_api_lock);

        if (time_till_next_ms < LVGL_TASK_MIN_DELAY_MS) {
            time_till_next_ms = LVGL_TASK_MIN_DELAY_MS;
        } else if (time_till_next_ms > LVGL_TASK_MAX_DELAY_MS) {
            time_till_next_ms = LVGL_TASK_MAX_DELAY_MS;
        }
        usleep(1000 * time_till_next_ms);
    }
}

/**
 * @brief Display initialization API, in two phases:
 *  PHASE 1: GC9A01 module (HAL + SW init sequence).
 *  PHASE 2: LVGL module (buffers, flush cb, tick timer).
 * @return 0 on success, -1 on failure.
 */
int8_t display_init(void) {
    if (display_init_lcd() != 0)  { return -1; }
    if (display_init_lvgl() != 0) { return -1; }
    return 0;
}