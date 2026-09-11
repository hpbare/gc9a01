#include "gc9a01.h"
#include "gc9a01_types.h"
#include "gc9a01_cmds.h"

#include <stddef.h>
#include <stdint.h>

#define GC9A01_CMD_BIT_WIDTH    8
#define GC9A01_CMD_BYTE_WIDTH   (GC9A01_CMD_BIT_WIDTH/8)

/**
 * @brief Transmit polling LCD command and corresponding parameters
 * @param[in] panel LCD panel handle.
 * @param[in] cmd The specific LCD command
 * @param[in] param Buffer that holds the command specific parameters, set to NULL if no parameter is needed for the command
 * @param[in] param_size Size of `param` in memory, in bytes, set to zero if no parameter is needed for the command
 * @return    `GC9A01_OK` on success, GC9A01_ERROR_INVALID_ARGS if parameter is invalid.
 */
static GC9A01_Status GC9A01_TransmitParamPolling(GC9A01_Panel *panel, GC9A01_LcdCmds cmd, const void *param, size_t param_size){
    GC9A01_Status s = GC9A01_OK;
    GC9A01_Hal *hal = panel->hal;
    uint8_t cmd_u8 = (uint8_t)cmd;

    s = hal->gpio_write(hal->CS, hal->flags.cs_active_level);
    if(s != GC9A01_OK) { goto release; }

    if(cmd_u8) {
        s = hal->gpio_write(hal->DC, hal->flags.dc_cmd_level);
        if(s != GC9A01_OK) { goto release; }
        s = hal->spi_polling.spi_transmit(hal->spi_ctx, &cmd_u8, GC9A01_CMD_BYTE_WIDTH);
        if(s != GC9A01_OK) { goto release; }
    }

    if(param && param_size){
        s = hal->gpio_write(hal->DC, hal->flags.dc_param_level);
        if(s != GC9A01_OK) { goto release; }
        s = hal->spi_polling.spi_transmit(hal->spi_ctx, param, param_size);
        if(s != GC9A01_OK) { goto release; }
    }

release:
    hal->gpio_write(hal->CS, !(hal->flags.cs_active_level));
    return s;
}


/**
 * @brief Transmit polling LCD RGB data
 * @param[in] panel LCD panel handle.
 * @param[in] cmd The specific LCD command
 * @param[in] color Buffer that holds the RGB color data
 * @param[in] color_size Size of `color` in memory, in bytes
 * @return    `GC9A01_OK` on success, GC9A01_ERROR_INVALID_ARGS if parameter is invalid.
 */
static GC9A01_Status GC9A01_TransmitColorPolling(GC9A01_Panel *panel, GC9A01_LcdCmds cmd, const void *color, size_t color_size){
    GC9A01_Status s = GC9A01_OK;
    GC9A01_Hal *hal = panel->hal;
    uint8_t cmd_u8 = (uint8_t)cmd;

    s = hal->gpio_write(hal->CS, hal->flags.cs_active_level);
    if(s != GC9A01_OK) { goto release; }

    if(cmd_u8) {
        s = hal->gpio_write(hal->DC, hal->flags.dc_cmd_level);
        if(s != GC9A01_OK) { goto release; }
        s = hal->spi_polling.spi_transmit(hal->spi_ctx, &cmd_u8, GC9A01_CMD_BYTE_WIDTH);
        if(s != GC9A01_OK) { goto release; }
    }

    if(color != NULL && color_size > 0){
        s = hal->gpio_write(hal->DC, hal->flags.dc_param_level);
        if(s != GC9A01_OK) { goto release; }

        while(color_size > 0){
            size_t chunk_size = (color_size > hal->spi_trans_max_bytes) ? hal->spi_trans_max_bytes : color_size;
            s = hal->spi_polling.spi_transmit(hal->spi_ctx, color, chunk_size);
            if(s != GC9A01_OK) { goto release; }

            color = (const uint8_t*)color + chunk_size;
            color_size -= chunk_size;
        }
    }

release:
    hal->gpio_write(hal->CS, !(hal->flags.cs_active_level));
    return s;
}

/* Reap exactly one oldest-completed transaction, decrement the counter. */
static GC9A01_Status GC9A01_ReapOneTransAsync(GC9A01_HalSpiAsync *async) {
    GC9A01_Status s = async->spi_get_trans_result(async, -1);
    if (s == GC9A01_OK && async->num_trans_inflight) {
        async->num_trans_inflight--;
    }
    return s;
}

/* Drain every in-flight transaction. Required before any polling (sync)
 * transmit on the same bus, since the queued chunks and the sync cmd
 * transfer physically share one SPI peripheral. */
static GC9A01_Status GC9A01_DrainAllTransAsync(GC9A01_HalSpiAsync *async) {
    GC9A01_Status s = GC9A01_OK;
    while (async->num_trans_inflight) {
        s = GC9A01_ReapOneTransAsync(async);
        if (s != GC9A01_OK) {
            return s;
        }
    }
    return GC9A01_OK;
}

/**
 * @brief Transmit LCD command + parameters. Command is sent synchronously
 *        (like the polling path); parameters are still small enough that
 *        sync transfer is fine and it lets us reuse the drain-before-sync
 *        rule below without a separate cmd-only descriptor.
 */
static GC9A01_Status GC9A01_TransmitParamAsync(GC9A01_Panel *panel, GC9A01_LcdCmds cmd, const void *param, size_t param_size) {
    GC9A01_Status s = GC9A01_OK;
    GC9A01_Hal *hal = panel->hal;
    GC9A01_HalSpiAsync *async = &hal->spi_async;
    uint8_t cmd_u8 = (uint8_t)cmd;

    s = async->spi_acquire_bus(hal->spi_ctx, -1);
    if (s != GC9A01_OK) { goto release; }

    /* Must be empty before any sync transfer touches the bus. */
    s = GC9A01_DrainAllTransAsync(async);
    if (s != GC9A01_OK) { goto release; }

    s = hal->gpio_write(hal->CS, hal->flags.cs_active_level);
    if (s != GC9A01_OK) { goto release; }

    if (cmd_u8) {
        s = hal->gpio_write(hal->DC, hal->flags.dc_cmd_level);
        if (s != GC9A01_OK) { goto release; }
        s = async->spi_transmit(hal->spi_ctx, &cmd_u8, GC9A01_CMD_BYTE_WIDTH);
        if (s != GC9A01_OK) { goto release; }
    }

    if (param && param_size) {
        s = hal->gpio_write(hal->DC, hal->flags.dc_param_level);
        if (s != GC9A01_OK) { goto release; }
        s = async->spi_transmit(hal->spi_ctx, param, param_size);
        if (s != GC9A01_OK) { goto release; }
    }

release:
    hal->gpio_write(hal->CS, !(hal->flags.cs_active_level));
    async->spi_release_bus(hal->spi_ctx);
    return s;
}

/**
 * @brief Transmit LCD RGB color data async. Command still sent sync/polling
 *        (same reasoning as GC9A01_TransmitParamAsync); color chunks are
 *        queued via spi_transmit_async, backpressure via queue_size.
 */
static GC9A01_Status GC9A01_TransmitColorAsync(GC9A01_Panel *panel, GC9A01_LcdCmds cmd, const void *color, size_t color_size) {
    GC9A01_Status s = GC9A01_OK;
    GC9A01_Hal *hal = panel->hal;
    GC9A01_HalSpiAsync *async = &hal->spi_async;
    uint8_t cmd_u8 = (uint8_t)cmd;

    s = async->spi_acquire_bus(hal->spi_ctx, -1);
    if (s != GC9A01_OK) { goto release; }

    s = GC9A01_DrainAllTransAsync(async);
    if (s != GC9A01_OK) { goto release; }

    s = hal->gpio_write(hal->CS, hal->flags.cs_active_level);
    if (s != GC9A01_OK) { goto release; }

    if (cmd_u8) {
        s = hal->gpio_write(hal->DC, hal->flags.dc_cmd_level);
        if (s != GC9A01_OK) { goto release; }
        s = async->spi_transmit(hal->spi_ctx, &cmd_u8, GC9A01_CMD_BYTE_WIDTH);
        if (s != GC9A01_OK) { goto release; }
    }

    if (color != NULL && color_size > 0) {
        s = hal->gpio_write(hal->DC, hal->flags.dc_param_level);
        if (s != GC9A01_OK) { goto release; }

        while (color_size > 0) {
            /* Backpressure: reap one oldest chunk if the queue is full. */
            if (async->num_trans_inflight >= async->queue_size) {
                s = GC9A01_ReapOneTransAsync(async);
                if (s != GC9A01_OK) { goto release; }
            }

            size_t chunk_size = (color_size > hal->spi_trans_max_bytes) ? hal->spi_trans_max_bytes : color_size;

            s = async->spi_transmit_async(hal->spi_ctx, color, chunk_size);
            if (s != GC9A01_OK) { goto release; }
            async->num_trans_inflight++;

            color = (const uint8_t *)color + chunk_size;
            color_size -= chunk_size;
        }
    }

release:
    hal->gpio_write(hal->CS, !(hal->flags.cs_active_level));
    async->spi_release_bus(hal->spi_ctx);
    return s;
}

GC9A01_Status GC9A01_TransmitParam(GC9A01_Panel *panel, GC9A01_LcdCmds cmd, const void *param, size_t param_size){
    if(panel->hal->type == GC9A01_SPI_TRANSMIT_TYPE_POLLING) {
        return GC9A01_TransmitParamPolling(panel, cmd, param, param_size);
    } else if(panel->hal->type == GC9A01_SPI_TRANSMIT_TYPE_ASYNC) {
        return GC9A01_TransmitParamAsync(panel, cmd, param, param_size);
    } else {
        return GC9A01_ERROR_INVALID_ARGS;
    }
}

/* Dispatcher pair for GC9A01_TransmitParam, symmetric with your existing one */
GC9A01_Status GC9A01_TransmitColor(GC9A01_Panel *panel, GC9A01_LcdCmds cmd, const void *color, size_t color_size) {
    if (panel->hal->type == GC9A01_SPI_TRANSMIT_TYPE_POLLING) {
        return GC9A01_TransmitColorPolling(panel, cmd, color, color_size);
    } else if (panel->hal->type == GC9A01_SPI_TRANSMIT_TYPE_ASYNC) {
        return GC9A01_TransmitColorAsync(panel, cmd, color, color_size);
    } else {
        return GC9A01_ERROR_INVALID_ARGS;
    }
}
