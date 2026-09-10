#include "gc9a01.h"

/**
 * @brief Default initializer for GC9A01_Hal - every pointer set to NULL,
 *        every scalar set to 0. Caller MUST fill in the function pointers
 *        and pins before use; this only guarantees no garbage/uninitialized
 *        stack values (dễ crash vì gọi nhầm con trỏ hàm rác).
 *
 * Usage:
 *   GC9A01_Hal hal = GC9A01_HAL_DEFAULT_CONFIG();
 *   hal.gpio_write = my_gpio_write;
 *   hal.spi_polling.spi_transmit = my_spi_transmit;
 *   ...
 */
#define GC9A01_HAL_DEFAULT_CONFIG()                              \
    {                                                            \
        .BLK                 = {0},                              \
        .DC                  = {0},                              \
        .RST                 = {0},                              \
        .CS                  = {0},                              \
        .gpio_reset          = NULL,                             \
        .gpio_write          = NULL,                             \
        .delay_ms            = NULL,                             \
        .flags               = {0},                              \
        .type                = GC9A01_SPI_TRANSMIT_TYPE_POLLING, \
        .spi_trans_max_bytes = 0,                                \
        .spi_ctx             = NULL,                             \
        .spi_polling         = {                                 \
            .spi_transmit    = NULL,                             \
            .spi_acquire_bus = NULL,                             \
            .spi_release_bus = NULL,                             \
        },                                                       \
    }

#define GC9A01_PANEL_DEFAULT_CONFIG()   \
    {                                       \
        .hal = GC9A01_HAL_DEFAULT_CONFIG(), \
        .madctl_val = 0x00,                 \
        .colmod_val = 0x00,                 \
        .init_cmds = NULL,                  \
        .init_cmds_size = 0,                \
        .ctx = NULL,                        \
    }

GC9A01_Panel *GC9A01_CreatePanel(void) {
    GC9A01_Panel *p = malloc(sizeof(GC9A01_Panel));
    if (!p) return NULL;
    *p = (GC9A01_Panel)GC9A01_PANEL_DEFAULT_CONFIG();
    return p;
}

void GC9A01_HalRegisterGpio(GC9A01_Panel *panel, GC9A01_Gpio DC, GC9A01_Gpio RST, GC9A01_Gpio CS, GC9A01_Gpio BLK) {
    panel->hal->DC  = DC;
    panel->hal->RST = RST;
    panel->hal->CS  = CS;
    panel->hal->BLK = BLK;
}

void GC9A01_HalRegisterLogicLevel(GC9A01_Panel *panel, bool dc_cmd_level, bool dc_param_level, bool cs_active_level, bool rst_level) {
    panel->hal->flags.dc_cmd_level    = dc_cmd_level;
    panel->hal->flags.dc_param_level  = dc_param_level;
    panel->hal->flags.cs_active_level = cs_active_level;
    panel->hal->flags.rst_level       = rst_level;
}

GC9A01_Status GC9A01_HalRegisterGpioApis(GC9A01_Panel *panel, GC9A01_GpioReset *gpio_reset, GC9A01_GpioWrite *gpio_write) {
    if(!gpio_reset || !gpio_write) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    panel->hal->gpio_reset = gpio_reset;
    panel->hal->gpio_write = gpio_write;
    return GC9A01_OK;
}

GC9A01_Status GC9A01_HalRegisterDelayMs(GC9A01_Panel *panel, GC9A01_DelayMs *delay_ms) {
    if(!delay_ms) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    panel->hal->delay_ms = delay_ms;
    return GC9A01_OK;
}

GC9A01_Status GC9A01_HalRegisterTransmitType(GC9A01_Panel *panel, GC9A01_SpiTransmitType type) {
    panel->hal->type = type;
    return GC9A01_OK;
}

GC9A01_Status GC9A01_HalRegisterSpiTransMaxBytes(GC9A01_Panel *panel, size_t spi_trans_max_bytes) {
    panel->hal->spi_trans_max_bytes = spi_trans_max_bytes;
    return GC9A01_OK;
}

GC9A01_Status GC9A01_HalRegisterSpiCtx(GC9A01_Panel *panel, void *spi_ctx) {
    panel->hal->spi_ctx = spi_ctx;
    return GC9A01_OK;
}

GC9A01_Status GC9A01_HalRegisterSpiTransmit(GC9A01_Panel *panel, GC9A01_SpiTransmit *spi_transmit) {
    if(!spi_transmit) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    if(panel->hal->type == GC9A01_SPI_TRANSMIT_TYPE_POLLING) {
        panel->hal->spi_polling.spi_transmit = spi_transmit;
    } else if(panel->hal->type == GC9A01_SPI_TRANSMIT_TYPE_ASYNC) {
        panel->hal->spi_async.spi_transmit = spi_transmit;
    } else {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    return GC9A01_OK;
}

GC9A01_Status GC9A01_HalRegisterSpiTransmitAsync(GC9A01_Panel *panel, GC9A01_SpiTransmitAsync *spi_transmit_async) {
    if(!spi_transmit_async || panel->hal->type != GC9A01_SPI_TRANSMIT_TYPE_ASYNC) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    panel->hal->spi_async.spi_transmit_async = spi_transmit_async;
    return GC9A01_OK;
}

GC9A01_Status GC9A01_HalRegisterSpiAcquireBus(GC9A01_Panel *panel, GC9A01_SpiAcquireBus *spi_acquire_bus) {
    if(!spi_acquire_bus) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    if(panel->hal->type == GC9A01_SPI_TRANSMIT_TYPE_POLLING) {
        panel->hal->spi_polling.spi_acquire_bus = spi_acquire_bus;
    } else if(panel->hal->type == GC9A01_SPI_TRANSMIT_TYPE_ASYNC) {
        panel->hal->spi_async.spi_acquire_bus = spi_acquire_bus;
    } else {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    return GC9A01_OK;
}

GC9A01_Status GC9A01_HalRegisterSpiReleaseBus(GC9A01_Panel *panel, GC9A01_SpiAcquireBus *spi_release_bus) {
    if(!spi_release_bus) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    if(panel->hal->type == GC9A01_SPI_TRANSMIT_TYPE_POLLING) {
        panel->hal->spi_polling.spi_release_bus = spi_release_bus;
    } else if(panel->hal->type == GC9A01_SPI_TRANSMIT_TYPE_ASYNC) {
        panel->hal->spi_async.spi_release_bus = spi_release_bus;
    } else {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    return GC9A01_OK;
}

GC9A01_Status GC9A01_HalRegisterQueueSize(GC9A01_Panel *panel, size_t queue_size) {
    if(panel->hal->type != GC9A01_SPI_TRANSMIT_TYPE_ASYNC) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    panel->hal->spi_async.queue_size = queue_size;
    return GC9A01_OK;
}


GC9A01_Status GC9A01_HalRegisterSpiGetTransResult(GC9A01_Panel *panel, GC9A01_SpiGetTransResult *spi_get_trans_result) {
    if(!spi_get_trans_result || panel->hal->type != GC9A01_SPI_TRANSMIT_TYPE_ASYNC) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    panel->hal->spi_async.spi_get_trans_result = spi_get_trans_result;
    return GC9A01_OK;
}

GC9A01_Status GC9A01_HalRegisterSpiRegisterTransDoneCb(GC9A01_Panel *panel, GC9A01_SpiRegisterTransDoneCb *register_spi_trans_done_cb) {
    if(!register_spi_trans_done_cb || panel->hal->type != GC9A01_SPI_TRANSMIT_TYPE_ASYNC) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    panel->hal->spi_async.register_spi_trans_done_cb = register_spi_trans_done_cb;
    return GC9A01_OK;
}