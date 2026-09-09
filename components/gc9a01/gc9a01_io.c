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
GC9A01_Status GC9A01_TransmitParamPolling(GC9A01_Panel *panel, GC9A01_SpiCmds cmd, const void *param, size_t param_size){
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
GC9A01_Status GC9A01_TransmitColorPolling(GC9A01_Panel *panel, GC9A01_SpiCmds cmd, const void *color, size_t color_size){
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


/**
 * @brief Transmit LCD command and corresponding parameters
 * @param[in] panel LCD panel handle.
 * @param[in] cmd The specific LCD command
 * @param[in] param Buffer that holds the command specific parameters, set to NULL if no parameter is needed for the command
 * @param[in] param_size Size of `param` in memory, in bytes, set to zero if no parameter is needed for the command
 * @return    `GC9A01_OK` on success, GC9A01_ERROR_INVALID_ARGS if parameter is invalid.
 */
GC9A01_Status GC9A01_TransmitParamAsync(GC9A01_Panel *panel, GC9A01_SpiCmds cmd, const void *param, size_t param_size){
    GC9A01_Status s = GC9A01_OK;
    GC9A01_Hal *hal = panel->hal;
    bool cs_active = false;
    uint8_t cmd_u8 = (uint8_t)cmd;

    /* PHASE 1: REFRESH */
    if(cmd_u8) {
        while(panel->num_trans_inflight > 0){
            GC9A01_Status drain_s = hal->spi_async.spi_get_trans_result(hal->spi_ctx, -1);
            panel->num_trans_inflight--;
            if(drain_s != GC9A01_OK && s == GC9A01_OK) { s = drain_s; }
        }
        if(s != GC9A01_OK) { return s; }
    }
    
    s = hal->gpio_write(hal->CS, hal->flags.cs_active_level);
    if(s != GC9A01_OK) { goto release; }
    cs_active = true;

    /* PHASE 3: SEND CMD - POLLING */
    if(cmd_u8) {
        s = hal->gpio_write(hal->DC, hal->flags.dc_cmd_level);
        if(s != GC9A01_OK) { goto release; }
        s = hal->spi_async.spi_transmit(hal->spi_ctx, &cmd_u8, GC9A01_CMD_BYTE_WIDTH);
        if(s != GC9A01_OK) { goto release; }
    }

    if(param && param_size){
        s = hal->gpio_write(hal->DC, hal->flags.dc_param_level);
        if(s != GC9A01_OK) { goto release; }
        s = hal->spi_async.spi_transmit(hal->spi_ctx, param, param_size);
        if(s != GC9A01_OK) { goto release; }
    }

release:
    if(cs_active){
        hal->gpio_write(hal->CS, !(hal->flags.cs_active_level));
    }

    return s;
}


/**
 * @brief Transmit LCD RGB data
 * @param[in] panel LCD panel handle.
 * @param[in] cmd The specific LCD command
 * @param[in] color Buffer that holds the RGB color data
 * @param[in] color_size Size of `color` in memory, in bytes
 * @return    `GC9A01_OK` on success, GC9A01_ERROR_INVALID_ARGS if parameter is invalid.
 */
GC9A01_Status GC9A01_TransmitColorAsync(GC9A01_Panel *panel, GC9A01_SpiCmds cmd, const void *color, size_t color_size){
    GC9A01_Status s = GC9A01_OK;
    GC9A01_Hal *hal = panel->hal;
    bool cs_active = false;
    uint8_t cmd_u8 = (uint8_t)cmd;

    /* PHASE 1: REFRESH — drain any async transactions from a previous call */
    if(cmd_u8) {
        while(panel->num_trans_inflight > 0){
            /* wait for OnTransactionDone callback - no manual drain anymore */
        }
    }

    s = hal->gpio_write(hal->CS, hal->flags.cs_active_level);
    if(s != GC9A01_OK) { goto release; }
    cs_active = true;

    /* PHASE 3: SEND CMD - POLLING */
    if(cmd_u8) {
        s = hal->gpio_write(hal->DC, hal->flags.dc_cmd_level);
        if(s != GC9A01_OK) { goto release; }
        s = hal->spi_async.spi_transmit(hal->spi_ctx, &cmd_u8, GC9A01_CMD_BYTE_WIDTH);
        if(s != GC9A01_OK) { goto release; }
    }

    /* PHASE 4: SEND COLOR */
    if(color != NULL && color_size > 0){
        s = hal->gpio_write(hal->DC, hal->flags.dc_param_level);
        if(s != GC9A01_OK) { goto release; }

        while(color_size > 0){
            size_t chunk_size = (color_size > hal->spi_trans_max_bytes) ? hal->spi_trans_max_bytes : color_size;
            panel->num_trans_inflight++;
            s = hal->spi_async.spi_transmit_async(hal->spi_ctx, color, chunk_size);
            if(s != GC9A01_OK) {
                panel->num_trans_inflight--;
                goto release;
            }

            color = (const uint8_t*)color + chunk_size;
            color_size -= chunk_size;
        }
    }

release:

    return s;
}

GC9A01_Status GC9A01_TransmitParam(GC9A01_Panel *panel, GC9A01_SpiCmds cmd, const void *param, size_t param_size){
    if(panel->hal->type == GC9A01_SPI_TRANSMIT_TYPE_POLLING) {
        return GC9A01_TransmitParamPolling(panel, cmd, param, param_size);
    } else if(panel->hal->type == GC9A01_SPI_TRANSMIT_TYPE_ASYNC) {
        return GC9A01_TransmitParamAsync(panel, cmd, param, param_size);
    } else {
        return GC9A01_ERROR_INVALID_ARGS;
    }
}

GC9A01_Status GC9A01_TransmitColor(GC9A01_Panel *panel, GC9A01_SpiCmds cmd, const void *color, size_t color_size){
    if(panel->hal->type == GC9A01_SPI_TRANSMIT_TYPE_POLLING) {
        return GC9A01_TransmitColorPolling(panel, cmd, color, color_size);
    } else if(panel->hal->type == GC9A01_SPI_TRANSMIT_TYPE_ASYNC) {
        return GC9A01_TransmitColorAsync(panel, cmd, color, color_size);
    } else {
        return GC9A01_ERROR_INVALID_ARGS;
    }
}

void GC9A01_OnTransactionDone(GC9A01_Panel *panel){
    panel->num_trans_inflight--;
    if(panel->num_trans_inflight == 0){
        panel->hal->gpio_write(panel->hal->CS, !(panel->hal->flags.cs_active_level));
    }
}














/**
 * @brief Transmit LCD command and corresponding parameters
 * @param[in] panel LCD panel handle.
 * @param[in] cmd The specific LCD command
 * @param[in] param Buffer that holds the command specific parameters, set to NULL if no parameter is needed for the command
 * @param[in] param_size Size of `param` in memory, in bytes, set to zero if no parameter is needed for the command
 * @return    `GC9A01_OK` on success, GC9A01_ERROR_INVALID_ARGS if parameter is invalid.
 */
GC9A01_Status GC9A01_TransmitParam(GC9A01_Panel *panel, GC9A01_SpiCmds cmd, const void *param, size_t param_size){
    GC9A01_Status s = GC9A01_OK;
    GC9A01_Hal *hal = panel->hal;
    bool spi_bus_control = (hal->spi_acquire_bus) && (hal->spi_release_bus);
    bool bus_acquired = false;
    bool cs_active = false;
    uint8_t cmd_u8 = (uint8_t)cmd;

    /* PHASE 1: REFRESH */
    if(cmd_u8) {
        while(panel->num_trans_inflight > 0){
            GC9A01_Status drain_s = hal->spi_get_trans_result(hal->spi_ctx, -1);
            panel->num_trans_inflight--;
            if(drain_s != GC9A01_OK && s == GC9A01_OK) { s = drain_s; }
        }
        if(s != GC9A01_OK) { return s; }
    }
    
    /* PHASE 2: ACQUIRE BUS, ACTIVE CS */
    if(spi_bus_control/*  && !(panel->bus_is_acquired) */){
        s = hal->spi_acquire_bus(hal->spi_ctx, -1);
        if (s != GC9A01_OK) { return s; }
        bus_acquired = true;
    }
    s = hal->gpio_write(hal->CS, hal->flags.cs_active_level);
    if(s != GC9A01_OK) { goto release; }
    cs_active = true;

    /* PHASE 3: SEND CMD - POLLING */
    if(cmd_u8) {
        s = hal->gpio_write(hal->DC, hal->flags.dc_cmd_level);
        if(s != GC9A01_OK) { goto release; }
        s = hal->spi_transmit(hal->spi_ctx, &cmd_u8, GC9A01_CMD_BYTE_WIDTH);
        if(s != GC9A01_OK) { goto release; }
    }

    if(param && param_size){
        s = hal->gpio_write(hal->DC, hal->flags.dc_param_level);
        if(s != GC9A01_OK) { goto release; }
        s = hal->spi_transmit(hal->spi_ctx, param, param_size);
        if(s != GC9A01_OK) { goto release; }
    }

release:
    if(cs_active){
        hal->gpio_write(hal->CS, !(hal->flags.cs_active_level));
    }

    if(bus_acquired){
        GC9A01_Status release_s = hal->spi_release_bus(hal->spi_ctx);
        if(s == GC9A01_OK) { return release_s; }
    }
    return s;
}

/**
 * @brief Transmit LCD RGB data
 * @param[in] panel LCD panel handle.
 * @param[in] cmd The specific LCD command
 * @param[in] color Buffer that holds the RGB color data
 * @param[in] color_size Size of `color` in memory, in bytes
 * @return    `GC9A01_OK` on success, GC9A01_ERROR_INVALID_ARGS if parameter is invalid.
 */
GC9A01_Status GC9A01_TransmitColor(GC9A01_Panel *panel, GC9A01_SpiCmds cmd, const void *color, size_t color_size){
    GC9A01_Status s = GC9A01_OK;
    GC9A01_Hal *hal = panel->hal;
    bool spi_bus_control = (hal->spi_acquire_bus) && (hal->spi_release_bus);
    bool async_capable = (hal->spi_transmit_async && hal->spi_get_trans_result);
    bool bus_acquired = false;
    bool cs_active = false;
    uint8_t cmd_u8 = (uint8_t)cmd;

    /* PHASE 1: REFRESH — drain any async transactions from a previous call */
    if(cmd_u8) {
        while(panel->num_trans_inflight > 0){
            GC9A01_Status drain_s = hal->spi_get_trans_result(hal->spi_ctx, -1);
            panel->num_trans_inflight--;
            if(drain_s != GC9A01_OK && s == GC9A01_OK) { s = drain_s; }
        }
        if(s != GC9A01_OK) { return s; }
    }

    /* PHASE 2: ACQUIRE BUS, ACTIVATE CS */
    if(spi_bus_control){
        s = hal->spi_acquire_bus(hal->spi_ctx, -1);
        if (s != GC9A01_OK) { return s; }
        bus_acquired = true;
    }
    s = hal->gpio_write(hal->CS, hal->flags.cs_active_level);
    if(s != GC9A01_OK) { goto release; }
    cs_active = true;

    /* PHASE 3: SEND CMD - POLLING */
    if(cmd_u8) {
        s = hal->gpio_write(hal->DC, hal->flags.dc_cmd_level);
        if(s != GC9A01_OK) { goto release; }
        s = hal->spi_transmit(hal->spi_ctx, &cmd_u8, GC9A01_CMD_BYTE_WIDTH);
        if(s != GC9A01_OK) { goto release; }
    }

    /* PHASE 4: SEND COLOR */
    if(color != NULL && color_size > 0){
        s = hal->gpio_write(hal->DC, hal->flags.dc_param_level);
        if(s != GC9A01_OK) { goto release; }

        while(color_size > 0){
            size_t chunk_size = (color_size > hal->spi_trans_max_bytes) ? hal->spi_trans_max_bytes : color_size;
            if(async_capable) {
                s = hal->spi_transmit_async(hal->spi_ctx, color, chunk_size);
                if(s != GC9A01_OK) { goto release; }
                panel->num_trans_inflight++;
            } else {
                s = hal->spi_transmit(hal->spi_ctx, color, chunk_size); /* Fallback path: works but costs CPU. */
                if(s != GC9A01_OK) { goto release; }
            }

            color = (const uint8_t*)color + chunk_size;
            color_size -= chunk_size;
        }
    }

release:
    if(cs_active && !(async_capable && panel->num_trans_inflight > 0)){
        hal->gpio_write(hal->CS, !(hal->flags.cs_active_level));
    }
    if(bus_acquired){
        GC9A01_Status release_s = hal->spi_release_bus(hal->spi_ctx);
        if(s == GC9A01_OK) { s = release_s; }
    }
    return s;
}

void GC9A01_OnTransactionDone(GC9A01_Panel *panel){
    panel->num_trans_inflight--;
    if(panel->num_trans_inflight == 0){
        panel->hal->gpio_write(panel->hal->CS, !(panel->hal->flags.cs_active_level));
    }
}
