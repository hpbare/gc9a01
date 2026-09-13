#include "gc9a01.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include <stdlib.h>
#include <string.h>

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
#define GC9A01_LCD_PIXEL_CLOCK_HZ   (40 * 1000 * 1000)
#define GC9A01_LCD_SPI_QUEUE_SIZE   10

#define GC9A01_SPI_MAX_TRANSFER_BYTES  (GC9A01_LCD_HRES * 80 * sizeof(uint16_t))

static GC9A01_Hal hal;
static GC9A01_Panel panel;
static spi_device_handle_t gc9a01_spi_handle;

static size_t gc9a01_max_trans_bytes = GC9A01_SPI_MAX_TRANSFER_BYTES;

/* Single global slot — matches the platform's own post_cb, which likewise
 * only supports one registrant. */
static GC9A01_TransDoneCb s_registered_trans_done_cb = NULL;

static SemaphoreHandle_t s_flush_done_sem;

/* ============================== HAL bindings (sync path — cmd/param) ============================== */

static GC9A01_Status gc9a01_hal_gpio_write(GC9A01_Gpio gpio, bool level) {
    if (gpio.pin < 0) {
        return GC9A01_OK;   /* CS now handled by SPI hardware, driver's own toggle is a no-op */
    }
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

/* ============================== HAL bindings (async path — color) ============================== */

/* trans_tag is stashed in spi_transaction_t.user; post_cb hands it back
 * unchanged when the transaction physically completes. */
static GC9A01_Status gc9a01_hal_spi_transmit_async(void *ctx, const void *tx, size_t len, void *trans_tag) {
    spi_transaction_t *t = malloc(sizeof(spi_transaction_t));
    if (!t) {
        return GC9A01_ERROR_SPI;
    }
    memset(t, 0, sizeof(*t));
    t->length    = len * 8;
    t->tx_buffer = tx;
    t->user      = trans_tag;

    if (spi_device_queue_trans((spi_device_handle_t)ctx, t, portMAX_DELAY) != ESP_OK) {
        free(t);
        return GC9A01_ERROR_SPI;
    }
    return GC9A01_OK;
}

static GC9A01_Status gc9a01_hal_spi_get_trans_result(void *ctx, int32_t ms) {
    spi_transaction_t *done;
    TickType_t ticks = (ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(ms);
    if (spi_device_get_trans_result((spi_device_handle_t)ctx, &done, ticks) != ESP_OK) {
        return GC9A01_ERROR_SPI;
    }
    free(done); /* malloc'd in gc9a01_hal_spi_transmit_async */
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

/* Platform's own post_cb — runs in ISR context whenever one queued
 * transaction finishes. Forwards trans->user to whoever the driver
 * registered via register_spi_trans_done_cb. */
static void IRAM_ATTR gc9a01_spi_post_trans_cb(spi_transaction_t *trans) {
    if (s_registered_trans_done_cb) {
        s_registered_trans_done_cb(trans->user);
    }
}

/* register_spi_trans_done_cb: driver calls this ONCE (from
 * GC9A01_RegisterEventCallbacks); we just remember which fn to call
 * from gc9a01_spi_post_trans_cb above. */
static void gc9a01_hal_register_trans_done_cb(GC9A01_TransDoneCb callback_function) {
    s_registered_trans_done_cb = callback_function;
}

/* ============================== App-level completion callback ============================== */

static void on_color_trans_done(GC9A01_Panel *p, void *user_ctx) {
    (void)p;
    (void)user_ctx;
    static volatile int count = 0;
    count++;
    esp_rom_printf("flush_done #%d\n", count);
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_done_sem, &woken);
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

/* ============================== GPIO setup ============================== */

static int gc9a01_configure_control_gpios(void) {
    gpio_config_t io_conf = {
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << GC9A01_GPIO_DC)  |
                        (1ULL << GC9A01_GPIO_RST) |
                        (1ULL << GC9A01_GPIO_BL),
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
        .spics_io_num   = GC9A01_GPIO_CS,
        .queue_size     = GC9A01_LCD_SPI_QUEUE_SIZE,
        .post_cb        = gc9a01_spi_post_trans_cb,
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
    GC9A01_Gpio cs  = { .ctx = NULL, .pin = -1 };
    GC9A01_Gpio bkl = { .ctx = NULL, .pin = GC9A01_GPIO_BL };

    GC9A01_HalSetGpio(&hal, dc, rst, cs, bkl);
    GC9A01_HalSetGpioApis(&hal, gc9a01_hal_gpio_reset, gc9a01_hal_gpio_write);
    GC9A01_HalSetLogicLevel(&hal, /*dc_cmd*/ 0, /*dc_param*/ 1, /*cs_active*/ 0, /*rst_active*/ 0);
    GC9A01_HalSetDelayMs(&hal, gc9a01_hal_delay_ms);

    GC9A01_HalSetTransmitType(&hal, GC9A01_SPI_TRANSMIT_TYPE_ASYNC);
    GC9A01_HalSetSpiTransMaxBytes(&hal, gc9a01_max_trans_bytes);
    GC9A01_HalSetSpiCtx(&hal, gc9a01_spi_handle);
    GC9A01_HalSetSpiTransmit(&hal, gc9a01_hal_spi_transmit);              /* cmd/param path */
    GC9A01_HalSetSpiTransmitAsync(&hal, gc9a01_hal_spi_transmit_async);   /* color path */
    GC9A01_HalSetSpiAcquireBus(&hal, gc9a01_hal_spi_acquire_bus);
    GC9A01_HalSetSpiReleaseBus(&hal, gc9a01_hal_spi_release_bus);
    GC9A01_HalSetQueueSize(&hal, GC9A01_LCD_SPI_QUEUE_SIZE);
    GC9A01_HalSetSpiGetTransResult(&hal, gc9a01_hal_spi_get_trans_result);
    GC9A01_HalSetSpiRegisterTransDoneCb(&hal, gc9a01_hal_register_trans_done_cb);

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
    if (GC9A01_DispSleep(&panel, false) != GC9A01_OK)     { ESP_LOGE(TAG, "Wake failed");     return -1; } /* SLPOUT — bắt buộc, Init() chỉ gửi SLPIN */
    if (GC9A01_Mirror(&panel, true, false) != GC9A01_OK)  { ESP_LOGE(TAG, "Mirror failed");   return -1; }
    if (GC9A01_DispOnOff(&panel, true) != GC9A01_OK)      { ESP_LOGE(TAG, "DispOnOff failed");return -1; }
    if (GC9A01_BacklightOnOff(&panel, true) != GC9A01_OK) { ESP_LOGE(TAG, "Backlight failed");return -1; }

    GC9A01_EventCallbacks cbs = { .on_color_trans_done = on_color_trans_done };
    if (GC9A01_RegisterEventCallbacks(&panel, &cbs, NULL) != GC9A01_OK) {
        ESP_LOGE(TAG, "RegisterEventCallbacks failed");
        return -1;
    }
    return 0;
}

static int display_init_gc9a01(void) {
    if (gc9a01_configure_control_gpios() != 0) { ESP_LOGE(TAG, "gpio config failed"); return -1; }
    if (display_init_gc9a01_spi() != 0)        { ESP_LOGE(TAG, "spi init failed");    return -1; }
    if (display_init_gc9a01_hal() != 0)        { ESP_LOGE(TAG, "hal/panel init failed"); return -1; }
    if (display_init_gc9a01_sequence() != 0)   { ESP_LOGE(TAG, "bring-up sequence failed"); return -1; }
    return 0;
}

/* ============================== Double-buffered render loop ============================== */

#define TEST_W GC9A01_LCD_HRES
#define TEST_H GC9A01_LCD_VRES

static uint16_t buf_a[TEST_W * TEST_H];
static uint16_t buf_b[TEST_W * TEST_H];

static void fill_solid(uint16_t *buf, uint16_t color) {
    for (int i = 0; i < TEST_W * TEST_H; i++) {
        buf[i] = color;
    }
}

void app_main(void) {
    s_flush_done_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(s_flush_done_sem); /* start "ready" so the first flush isn't blocked */

    if (display_init_gc9a01() != 0) {
        ESP_LOGE(TAG, "display_init_gc9a01 failed");
        return;
    }

    uint16_t *front = buf_a;
    uint16_t *back  = buf_b;
    uint16_t color  = 0xF800;

    while (1) {
        /* Wait until the buffer we're about to draw into is no longer
         * being read by DMA — i.e. the LAST flush that used it has fired
         * on_color_trans_done. */
        xSemaphoreTake(s_flush_done_sem, portMAX_DELAY);

        fill_solid(back, color);
        color = (color == 0xF800) ? 0x07E0 : 0xF800; /* alternate red/green */

        GC9A01_Status st = GC9A01_DrawBitmap(&panel, 0, 0, TEST_W, TEST_H, back);
        if (st != GC9A01_OK) {
            ESP_LOGE(TAG, "DrawBitmap failed: %d", (int)st);
            xSemaphoreGive(s_flush_done_sem); /* don't deadlock the loop on error */
        }
        /* Returns immediately after queueing — does NOT wait for DMA.
         * Swap buffers now; 'front' (in flight) must not be touched again
         * until on_color_trans_done gives the semaphore back. */
        uint16_t *tmp = front;
        front = back;
        back  = tmp;

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}