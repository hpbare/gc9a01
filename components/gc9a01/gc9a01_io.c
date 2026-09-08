#include "gc9a01_types.h"
#include "gc9a01_cmds.h"

#include <stddef.h>
#include <stdint.h>

#define GC9A01_CMD_BIT_WIDTH    8
#define GC9A01_CMD_BYTE_WIDTH   (GC9A01_CMD_BIT_WIDTH/8)

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
    bool async_capable = (hal->spi_transmit_async && hal->spi_get_trans_result);
    uint8_t cmd_u8 = (uint8_t)cmd;

    /* PHASE 1: REFRESH */
    if(cmd_u8) {
        while(panel->num_trans_inflight > 0){
            GC9A01_Status drain_s = hal->spi_get_trans_result(hal->spi_ctx, -1);
            panel->num_trans_inflight--;
            if(drain_s != GC9A01_OK && s == GC9A01_OK) { s = drain_s; }
        }

        if(panel->cs_is_active){
            hal->gpio_write(hal->CS, !(hal->flags.cs_active_level));
            panel->cs_is_active = false;
        }

        if(panel->bus_is_acquired && spi_bus_control){
            hal->spi_release_bus(hal->spi_ctx);
            panel->bus_is_acquired = false;
        }
        if(s != GC9A01_OK) { return s; }
    }
    
    /* PHASE 2: ACQUIRE BUS, ACTIVE CS */
    if(spi_bus_control && !(panel->bus_is_acquired)){
        s = hal->spi_acquire_bus(hal->spi_ctx, -1);
        if (s != GC9A01_OK) { return s; }
        panel->bus_is_acquired = true;
    }
    if(!(panel->cs_is_active)){
        s = hal->gpio_write(hal->CS, hal->flags.cs_active_level);
        if(s != GC9A01_OK) { goto release; }
        panel->cs_is_active = true;
    }

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
    if(spi_bus_control){
        s = hal->spi_release_bus(hal->spi_ctx);
    }
    panel->bus_is_acquired = false;
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
    uint8_t cmd_u8 = (uint8_t)cmd;

    /* PHASE 1: REFRESH */
    if(cmd_u8) {
        while(panel->num_trans_inflight > 0){
            GC9A01_Status drain_s = hal->spi_get_trans_result(hal->spi_ctx, -1);
            panel->num_trans_inflight--;
            if(drain_s != GC9A01_OK && s == GC9A01_OK) { s = drain_s; }
        }

        if(panel->cs_is_active){
            hal->gpio_write(hal->CS, !(hal->flags.cs_active_level));
            panel->cs_is_active = false;
        }

        if(panel->bus_is_acquired && spi_bus_control){
            hal->spi_release_bus(hal->spi_ctx);
            panel->bus_is_acquired = false;
        }
        if(s != GC9A01_OK) { return s; }
    }
    
    /* PHASE 2: ACQUIRE BUS, ACTIVE CS */
    if(spi_bus_control && !(panel->bus_is_acquired)){
        s = hal->spi_acquire_bus(hal->spi_ctx, -1);
        if (s != GC9A01_OK) { return s; }
        panel->bus_is_acquired = true;
    }
    if(!(panel->cs_is_active)){
        s = hal->gpio_write(hal->CS, hal->flags.cs_active_level);
        if(s != GC9A01_OK) { goto release; }
        panel->cs_is_active = true;
    }

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
                s = hal->spi_transmit(hal->spi_ctx, color, chunk_size); /* Không mong muốn vào nhánh này, chạy được nhưng tốn CPU. */
                if(s != GC9A01_OK) { goto release; }
            }

            color = (const uint8_t*)color + chunk_size; /* Increase address to next chunk */
            color_size -= chunk_size;
        }
    }

release:
    if(spi_bus_control){
        s = hal->spi_release_bus(hal->spi_ctx);
    }
    panel->bus_is_acquired = false;
    return s;
}

void GC9A01_OnTransactionDone(GC9A01_Panel *panel){
    panel->hal->gpio_write(panel->hal->CS, !(panel->hal->flags.cs_active_level));
    panel->cs_is_active = false;
}
