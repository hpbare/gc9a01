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
    GC9A01_ERROR_INVALID_ARGS  = -1,
    GC9A01_ERROR_NOT_SUPPORTED = -2,
    GC9A01_ERROR_SPI           = -3,
} GC9A01_Status;

typedef struct {
    void *ctx;
    int32_t pin;
} GC9A01_Gpio;

typedef void          (*GC9A01_DelayMs)(uint32_t ms);

typedef GC9A01_Status (*GC9A01_GpioWrite)(GC9A01_Gpio gpio, bool level);
typedef GC9A01_Status (*GC9A01_GpioReset)(GC9A01_Gpio gpio);
typedef GC9A01_Status (*GC9A01_SpiTransmit)(void *ctx, const void *tx, size_t len);

typedef GC9A01_Status (*GC9A01_SpiTransmitAsync)(void *ctx, const void *tx, size_t len);
typedef GC9A01_Status (*GC9A01_SpiAcquireBus)(void *ctx, int32_t timeout_ms);
typedef GC9A01_Status (*GC9A01_SpiReleaseBus)(void *ctx);
typedef GC9A01_Status (*GC9A01_SpiGetTransResult)(void *ctx, int32_t ms);

typedef void (*GC9A01_TransDoneCb)(void *ctx);
typedef void (*GC9A01_SpiRegisterTransDoneCb)(GC9A01_TransDoneCb callback_function, void *args); 

typedef struct {
    uint32_t dc_cmd_level   : 1;
    uint32_t dc_param_level : 1;
    uint32_t cs_active_level: 1;
    uint32_t rst_level      : 1;
} GC9A01_Flags;

typedef struct {
    int cmd;
    const void *data;
    size_t data_bytes;
    unsigned int delay_ms;
} GC9A01_InitCmd;

typedef enum {
    GC9A01_SPI_TRANSMIT_TYPE_POLLING = 0,
    GC9A01_SPI_TRANSMIT_TYPE_ASYNC   = 1,
} GC9A01_SpiTransmitType;

typedef struct {
    GC9A01_SpiTransmit            spi_transmit;
    GC9A01_SpiAcquireBus          spi_acquire_bus;
    GC9A01_SpiReleaseBus          spi_release_bus;
} GC9A01_HalSpiPolling;

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

typedef struct {
    GC9A01_Gpio BLK;
    GC9A01_Gpio DC;
    GC9A01_Gpio RST;
    GC9A01_Gpio CS;
    GC9A01_GpioReset         gpio_reset;
    GC9A01_GpioWrite         gpio_write;
    GC9A01_DelayMs           delay_ms;
    GC9A01_Flags             flags;
    GC9A01_SpiTransmitType   type;

    size_t                   spi_trans_max_bytes;
    void *spi_ctx;

    union {
        GC9A01_HalSpiPolling spi_polling;
        GC9A01_HalSpiAsync   spi_async;
    };
} GC9A01_Hal;

typedef struct {
    GC9A01_Hal *hal;

    uint8_t madctl_val;         // save current value of LCD_CMD_MADCTL register
    uint8_t colmod_val;         // save current value of LCD_CMD_COLMOD register
    GC9A01_InitCmd *init_cmds;
    uint16_t init_cmds_size;
    /** @brief User context, spi handle for example. */
    void *ctx;
} GC9A01_Panel;

#ifdef __cplusplus
}
#endif

#endif /* GC9A01_TYPES_H_ */