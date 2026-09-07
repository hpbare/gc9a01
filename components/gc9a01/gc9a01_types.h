#ifndef GC9A01_TYPES_H_
#define GC9A01_TYPES_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GC9A01_OK                  = 0,
    GC9A01_ERROR_NOT_SUPPORTED = -1,
    GC9A01_ERROR_SPI           = -2
} GC9A01_Status;

typedef struct {
    void *ctx;
    int32_t pin;
} GC9A01_Gpio;

typedef GC9A01_Status (*GC9A01_GpioWrite)(GC9A01_Gpio gpio, bool level);
typedef GC9A01_Status (*GC9A01_Spi)(void *ctx, const void *tx, size_t len);
typedef GC9A01_Status (*GC9A01_SpiRequireBus)(void *ctx, int32_t timeout_ms);
typedef GC9A01_Status (*GC9A01_SpiReleaseBus)(void *ctx);
typedef GC9A01_Status (*GC9A01_SpiGetTransResult)(void *ctx, int32_t ms);

typedef struct {
    uint32_t dc_cmd_level   : 1;
    uint32_t dc_param_level : 1;
    // uint32_t octal_mode     : 1;
} GC9A01_Flags;

typedef struct {
    GC9A01_Gpio BLK;
    GC9A01_Gpio DC;
    GC9A01_Gpio RST;
    GC9A01_Gpio CS;
    GC9A01_GpioWrite         gpio_write;
    GC9A01_Flags             flags;

    GC9A01_Spi               spi_transmit;
    GC9A01_Spi               spi_transmit_async;
    GC9A01_SpiRequireBus     spi_require_bus;
    GC9A01_SpiReleaseBus     spi_release_bus;
    GC9A01_SpiGetTransResult spi_get_trans_result;
    void *spi_ctx;
} GC9A01_Hal;

typedef struct {
    GC9A01_Hal *hal;
    size_t num_trans_inflight;
    /** @brief User context, spi handle for example. */
    void *ctx;
} GC9A01_Panel;

#ifdef __cplusplus
}
#endif

#endif /* GC9A01_TYPES_H_ */