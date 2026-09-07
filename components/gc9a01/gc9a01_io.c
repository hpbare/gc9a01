#include "gc9a01_types.h"
#include "gc9a01_cmds.h"

#include <stddef.h>
#include <stdint.h>

#define GC9A01_CMD_BIT_WIDTH    8
#define GC9A01_CMD_BYTE_WIDTH   (GC9A01_CMD_BIT_WIDTH/8)

static GC9A01_ReapPendingTrans(GC9A01_Panel *panel){
    GC9A01_Status s = GC9A01_OK;
    GC9A01_Hal *hal = panel->hal;

    if(hal->spi_get_trans_result){
        while(panel->num_trans_inflight > 0){
            s = hal->spi_get_trans_result(hal->spi_ctx, -1);
            if(s != GC9A01_OK) { return s; }
            panel->num_trans_inflight--;
        }
    }

    return s;
}

/** Octal mode is not implemented yet */
static GC9A01_Status GC9A01_TransmitParam(GC9A01_Panel *panel, GC9A01_SpiCmds cmd, const void *param, size_t param_size){
    GC9A01_Status s = GC9A01_OK;
    GC9A01_Hal *hal = panel->hal;
    bool spi_release_require_enable = (hal->spi_require_bus) && (hal->spi_release_bus);

    if(spi_release_require_enable){
        s = hal->spi_require_bus(hal->spi_ctx, -1);
        if(s != GC9A01_OK) { return s; }
    }

    s = GC9A01_ReapPendingTrans(panel);
    if(s != GC9A01_OK) { goto release; }

    if(cmd != GC9A01_LCD_CMD_NOP){
        uint8_t cmd_u8 = (uint8_t)cmd;
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
    if(spi_release_require_enable){
        hal->spi_release_bus(hal->spi_ctx);
    }

    return s;    
}

/** Octal mode is not implemented yet */
static GC9A01_Status GC9A01_TransmitColor(GC9A01_Panel *panel, GC9A01_SpiCmds cmd, const void *color, size_t color_size){
    GC9A01_Status s = GC9A01_OK;
    GC9A01_Hal *hal = panel->hal;
    bool spi_release_require_enable = (hal->spi_require_bus) && (hal->spi_release_bus);

    if(spi_release_require_enable){
        s = hal->spi_require_bus(hal->spi_ctx, -1);
        if(s != GC9A01_OK) { return s; }
    }

    s = GC9A01_ReapPendingTrans(panel);
    if(s != GC9A01_OK) { goto release; }

    if(cmd != GC9A01_LCD_CMD_NOP){
        uint8_t cmd_u8 = (uint8_t)cmd;
        s = hal->gpio_write(hal->DC, hal->flags.dc_cmd_level);
        if(s != GC9A01_OK) { goto release; }
        s = hal->spi_transmit(hal->spi_ctx, &cmd_u8, GC9A01_CMD_BYTE_WIDTH);
        if(s != GC9A01_OK) { goto release; }
    }

    if(color_size > 0){
        size_t chunk_size = color_size;
    }



release:
    if(spi_release_require_enable){
        hal->spi_release_bus(hal->spi_ctx);
    }

    return s;    
}