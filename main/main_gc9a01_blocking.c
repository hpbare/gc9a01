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
#define GC9A01_LCD_PIXEL_CLOCK_HZ   (40 * 1000 * 1000)
#define GC9A01_LCD_SPI_QUEUE_SIZE   1   /* polling doesn't queue, but field is required */

#define GC9A01_SPI_MAX_TRANSFER_BYTES  (GC9A01_LCD_HRES * 80 * sizeof(uint16_t))

static GC9A01_Hal hal;
static GC9A01_Panel panel;
static spi_device_handle_t gc9a01_spi_handle;

/* Runtime cap for a single SPI transaction — hardware length-register limit
 * can be smaller than max_transfer_sz, queried below after bus init. */
static size_t gc9a01_max_trans_bytes = GC9A01_SPI_MAX_TRANSFER_BYTES;

/* ============================== HAL bindings ============================== */

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

/* ============================== GPIO setup ============================== */

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
     * struct in panel->config — a stack local would dangle after return. */
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
    return 0;
}

static int display_init_gc9a01(void) {
    if (gc9a01_configure_control_gpios() != 0) { ESP_LOGE(TAG, "gpio config failed"); return -1; }
    if (display_init_gc9a01_spi() != 0)        { ESP_LOGE(TAG, "spi init failed");    return -1; }
    if (display_init_gc9a01_hal() != 0)        { ESP_LOGE(TAG, "hal/panel init failed"); return -1; }
    if (display_init_gc9a01_sequence() != 0)   { ESP_LOGE(TAG, "bring-up sequence failed"); return -1; }
    return 0;
}

/* ============================== Test pattern / render loop ============================== */

#define TEST_W GC9A01_LCD_HRES
#define TEST_H GC9A01_LCD_VRES
static uint16_t test_buf[TEST_W * TEST_H];

static void fill_solid(uint16_t color) {
    for (int i = 0; i < TEST_W * TEST_H; i++) {
        test_buf[i] = color;
    }
}

void app_main(void) {
    if (display_init_gc9a01() != 0) {
        ESP_LOGE(TAG, "display_init_gc9a01 failed");
        return;
    }

    uint16_t color = 0xF800; /* red */
    while (1) {
        fill_solid(color);

        GC9A01_Status st = GC9A01_DrawBitmap(&panel, 0, 0, TEST_W, TEST_H, test_buf);
        /* DrawBitmap chỉ return sau khi đã truyền xong THẬT trên bus —
         * an toàn tái sử dụng test_buf ngay lập tức, không cần chờ gì thêm. */
        if (st != GC9A01_OK) {
            ESP_LOGE(TAG, "DrawBitmap failed: %d", (int)st);
        }

        color = (color == 0xF800) ? 0x07E0 : 0xF800; /* alternate red/green */
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}