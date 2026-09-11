#ifndef GC9A01_TYPES_H_
#define GC9A01_TYPES_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief GC9A01 driver status code. */
typedef enum {
    GC9A01_OK                  = 0,
    GC9A01_ERROR_INVALID_ARGS  = -1,
    GC9A01_ERROR_NOT_SUPPORTED = -2,
    GC9A01_ERROR_SPI           = -3,
    GC9A01_ERROR_GPIO          = -4
} GC9A01_Status;

/** @brief Generic GPIO handle. */
typedef struct {
    void *ctx;      /**<! Platform specific (e.g. GPIO port struct). */
    int32_t pin;    /**<! Pin number/mask of platform. */
} GC9A01_Gpio;

/** @brief Blocking delay in ms. @param ms delay time in ms. */
typedef void          (*GC9A01_DelayMs)(uint32_t ms);

/** @brief Set GPIO level. */
typedef GC9A01_Status (*GC9A01_GpioWrite)(GC9A01_Gpio gpio, bool level);

/** @brief Reset GPIO. */
typedef GC9A01_Status (*GC9A01_GpioReset)(GC9A01_Gpio gpio);

/** @brief Blocking SPI transmit. */
typedef GC9A01_Status (*GC9A01_SpiTransmit)(void *ctx, const void *tx, size_t len);

/** @brief Async SPI transmit. */
typedef GC9A01_Status (*GC9A01_SpiTransmitAsync)(void *ctx, const void *tx, size_t len);

/** @brief Acquire exclusive access to the shared SPI bus (timeout in ms, -1 = wait forever). */
typedef GC9A01_Status (*GC9A01_SpiAcquireBus)(void *ctx, int32_t timeout_ms);

/** @brief Release SPI bus. */
typedef GC9A01_Status (*GC9A01_SpiReleaseBus)(void *ctx);

/** @brief Wait up to `ms` for an in-flight async transaction to finish. */
typedef GC9A01_Status (*GC9A01_SpiGetTransResult)(void *ctx, int32_t ms);

/** @brief Called by the HAL when an async SPI transaction completes. */
typedef void          (*GC9A01_TransDoneCb)(void *ctx);

/** @brief Registers `callback_function` to be invoked (with `args`) on transaction completion. */
typedef void          (*GC9A01_SpiRegisterTransDoneCb)(GC9A01_TransDoneCb callback_function, void *args); 

/** @brief Active-level/polarity configuration for control pins. */
typedef struct {
    uint32_t dc_cmd_level   : 1;  /* DC pin level that selects "command" mode */
    uint32_t dc_param_level : 1;  /* DC pin level that selects "data/param" mode */
    uint32_t cs_active_level: 1;  /* CS pin level considered "active" (chip selected) */
    uint32_t rst_level      : 1;  /* RST pin level that triggers reset */
} GC9A01_Flags;

/** @brief Internal driver state, cached to avoid unnecessary register writes. */
typedef struct {
    uint8_t madctl_val;     /* save current value of MADCTL register */
    uint8_t colmod_val;     /* save current value of COLMOD register */
} GC9A01_Internal;

/** @brief RGB data endian. */
typedef enum {
    GC9A01_RGB_DATA_ENDIAN_BIG    = 0,  /*!< RGB data endian: MSB first */
    GC9A01_RGB_DATA_ENDIAN_LITTLE = 1   /*!< RGB data endian: LSB first */
} GC9A01_RgbDataEndian;

/** @brief RGB element order. */
typedef enum {
    GC9A01_RGB_ELEMENT_ORDER_RGB = 0,   /*!< RGB element order: RGB */
    GC9A01_RGB_ELEMENT_ORDER_BGR = 1    /*!< RGB element order: BGR */
} GC9A01_RgbElementOrder;

/** @brief Panel configurations. */
typedef struct {
    uint32_t                bits_per_pixel;     /* bit depth sent to the panel over SPI. */
    int                     x_gap;              /* horizontal offset into panel RAM. */
    int                     y_gap;              /* vertical offset into panel RAM. */
    uint8_t                 fb_bits_per_pixels; /* bit depth of the source framebuffer. */
    GC9A01_RgbElementOrder  rgb_element_order;
    GC9A01_RgbDataEndian    data_endian;
} GC9A01_Config;

/** @brief Selects which member of the spi_polling/spi_async union is active. */
typedef enum {
    GC9A01_SPI_TRANSMIT_TYPE_POLLING = 0,
    GC9A01_SPI_TRANSMIT_TYPE_ASYNC   = 1
} GC9A01_SpiTransmitType;

/** @brief HAL SPI hooks for blocking (polling) transfers. */
typedef struct {
    GC9A01_SpiTransmit            spi_transmit;
    GC9A01_SpiAcquireBus          spi_acquire_bus;
    GC9A01_SpiReleaseBus          spi_release_bus;
} GC9A01_HalSpiPolling;

/** @brief HAL SPI hooks for non-blocking (async/queued) transfers. */
typedef struct {
    size_t                        num_trans_inflight;
    size_t                        queue_size;
    GC9A01_SpiTransmit            spi_transmit;
    GC9A01_SpiTransmitAsync       spi_transmit_async;
    GC9A01_SpiAcquireBus          spi_acquire_bus;
    GC9A01_SpiReleaseBus          spi_release_bus;
    GC9A01_SpiGetTransResult      spi_get_trans_result;
    GC9A01_SpiRegisterTransDoneCb register_spi_trans_done_cb;
} GC9A01_HalSpiAsync;

/** @brief Top-level HAL injected into the driver. */
typedef struct {
    GC9A01_Gpio             BLK;                    /* backlight control pin */
    GC9A01_Gpio             DC;                     /* data/command select pin */
    GC9A01_Gpio             RST;                    /* hardware reset pin */
    GC9A01_Gpio             CS;                     /* chip select pin */
    GC9A01_GpioReset        gpio_reset;
    GC9A01_GpioWrite        gpio_write;
    GC9A01_DelayMs          delay_ms;
    GC9A01_Flags            flags;
    GC9A01_SpiTransmitType  type;                /* picks polling vs async union member */
    size_t                  spi_trans_max_bytes; /* max bytes per single SPI transaction */
    void                    *spi_ctx;            /* opaque handle passed to all SPI callbacks */
    union {
        GC9A01_HalSpiPolling spi_polling;
        GC9A01_HalSpiAsync   spi_async;
    };
} GC9A01_Hal;

#ifdef __cplusplus
}
#endif

#endif /* GC9A01_TYPES_H_ */