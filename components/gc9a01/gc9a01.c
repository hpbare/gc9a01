#include "gc9a01.h"
#include "gc9a01_cmds.h"

/** LAYER 1: IO */

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

static GC9A01_Status GC9A01_TransmitParam(GC9A01_Panel *panel, GC9A01_LcdCmds cmd, const void *param, size_t param_size){
    if(panel->hal->type == GC9A01_SPI_TRANSMIT_TYPE_POLLING) {
        return GC9A01_TransmitParamPolling(panel, cmd, param, param_size);
    } else if(panel->hal->type == GC9A01_SPI_TRANSMIT_TYPE_ASYNC) {
        return GC9A01_TransmitParamAsync(panel, cmd, param, param_size);
    } else {
        return GC9A01_ERROR_INVALID_ARGS;
    }
}

/* Dispatcher pair for GC9A01_TransmitParam, symmetric with your existing one */
static GC9A01_Status GC9A01_TransmitColor(GC9A01_Panel *panel, GC9A01_LcdCmds cmd, const void *color, size_t color_size) {
    if (panel->hal->type == GC9A01_SPI_TRANSMIT_TYPE_POLLING) {
        return GC9A01_TransmitColorPolling(panel, cmd, color, color_size);
    } else if (panel->hal->type == GC9A01_SPI_TRANSMIT_TYPE_ASYNC) {
        return GC9A01_TransmitColorAsync(panel, cmd, color, color_size);
    } else {
        return GC9A01_ERROR_INVALID_ARGS;
    }
}




/* LAYER 2: PANEL */

typedef struct {
    int cmd;
    const void *data;
    size_t data_bytes;
    unsigned int delay_ms;
} GC9A01_InitCmd;

static const GC9A01_InitCmd init_cmds_default[] = {
    /* {cmd, { data }, data_size, delay_ms} */
    /* Enable Inter Register */
    {0xfe, (uint8_t []){0x00},                                                                      0,  0},
    {0xef, (uint8_t []){0x00},                                                                      0,  0},
    {0xeb, (uint8_t []){0x14},                                                                      1,  0},
    {0x84, (uint8_t []){0x60},                                                                      1,  0},
    {0x85, (uint8_t []){0xff},                                                                      1,  0},
    {0x86, (uint8_t []){0xff},                                                                      1,  0},
    {0x87, (uint8_t []){0xff},                                                                      1,  0},
    {0x8e, (uint8_t []){0xff},                                                                      1,  0},
    {0x8f, (uint8_t []){0xff},                                                                      1,  0},
    {0x88, (uint8_t []){0x0a},                                                                      1,  0},
    {0x89, (uint8_t []){0x23},                                                                      1,  0},
    {0x8a, (uint8_t []){0x00},                                                                      1,  0},
    {0x8b, (uint8_t []){0x80},                                                                      1,  0},
    {0x8c, (uint8_t []){0x01},                                                                      1,  0},
    {0x8d, (uint8_t []){0x03},                                                                      1,  0},
    {0x90, (uint8_t []){0x08, 0x08, 0x08, 0x08},                                                    4,  0},
    {0xff, (uint8_t []){0x60, 0x01, 0x04},                                                          3,  0},
    {0xC3, (uint8_t []){0x13},                                                                      1,  0},
    {0xC4, (uint8_t []){0x13},                                                                      1,  0},
    {0xC9, (uint8_t []){0x30},                                                                      1,  0},
    {0xbe, (uint8_t []){0x11},                                                                      1,  0},
    {0xe1, (uint8_t []){0x10, 0x0e},                                                                2,  0},
    {0xdf, (uint8_t []){0x21, 0x0c, 0x02},                                                          3,  0},
    // Set gamma
    {0xF0, (uint8_t []){0x45, 0x09, 0x08, 0x08, 0x26, 0x2a},                                        6,  0},
    {0xF1, (uint8_t []){0x43, 0x70, 0x72, 0x36, 0x37, 0x6f},                                        6,  0},
    {0xF2, (uint8_t []){0x45, 0x09, 0x08, 0x08, 0x26, 0x2a},                                        6,  0},
    {0xF3, (uint8_t []){0x43, 0x70, 0x72, 0x36, 0x37, 0x6f},                                        6,  0},
    {0xed, (uint8_t []){0x1b, 0x0b},                                                                2,  0},
    {0xae, (uint8_t []){0x77},                                                                      1,  0},
    {0xcd, (uint8_t []){0x63},                                                                      1,  0},
    {0x70, (uint8_t []){0x07, 0x07, 0x04, 0x0e, 0x0f, 0x09, 0x07, 0x08, 0x03},                      9,  0},
    {0xE8, (uint8_t []){0x34},                                                                      1,  0}, // 4 dot inversion
    {0x60, (uint8_t []){0x38, 0x0b, 0x6D, 0x6D, 0x39, 0xf0, 0x6D, 0x6D},                            8,  0},
    {0x61, (uint8_t []){0x38, 0xf4, 0x6D, 0x6D, 0x38, 0xf7, 0x6D, 0x6D},                            8,  0},
    {0x62, (uint8_t []){0x38, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x38, 0x0F, 0x71, 0xEF, 0x70, 0x70},    12, 0},
    {0x63, (uint8_t []){0x38, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x38, 0x13, 0x71, 0xF3, 0x70, 0x70},    12, 0},
    {0x64, (uint8_t []){0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07},                                  7,  0},
    {0x66, (uint8_t []){0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00},                10, 0},
    {0x67, (uint8_t []){0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98},                10, 0},
    {0x74, (uint8_t []){0x10, 0x45, 0x80, 0x00, 0x00, 0x4E, 0x00},                                  7,  0},
    {0x98, (uint8_t []){0x3e, 0x07},                                                                2,  0},
    {0x99, (uint8_t []){0x3e, 0x07},                                                                2,  0},
};

/** 
 * @brief Reset LCD panel.
 * @param[in] panel LCD panel handle.
 * @return `GC9A01_OK` on success.
 */
GC9A01_Status GC9A01_Reset(GC9A01_Panel *panel) {
    if(!panel || !(panel->hal)) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    GC9A01_Status s = GC9A01_OK;
    GC9A01_Hal *hal = panel->hal;

    if(hal->RST.pin >= 0){
        hal->gpio_write(hal->RST, hal->flags.rst_level);
        hal->delay_ms(10);
        hal->gpio_write(hal->RST, !(hal->flags.rst_level));
        hal->delay_ms(10);
    } else {
        s = GC9A01_TransmitParam(panel, GC9A01_LCD_CMD_SWRESET, NULL, 0);
        if(s != GC9A01_OK) { return -1; }
        panel->hal->delay_ms(20);
    }

    return s;
}

/**
 * @brief Initialize LCD panel.
 * @param[in] panel LCD panel handle.
 * @return `GC9A01_OK` on success.
 */
GC9A01_Status GC9A01_Init(GC9A01_Panel *panel) {
    if(!panel || !(panel->hal)) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    GC9A01_Status s = GC9A01_OK;
    GC9A01_Hal *hal = panel->hal;

    s = GC9A01_TransmitParam(panel, GC9A01_LCD_CMD_SLPIN, NULL, 0);
    if(s != GC9A01_OK) { return s; }
    hal->delay_ms(100);

    s = GC9A01_TransmitParam(panel, GC9A01_LCD_CMD_MADCTL, (uint8_t[]){panel->state.colmod_val, 1}, 0);
    if(s != GC9A01_OK) { return s; }

    s = GC9A01_TransmitParam(panel, GC9A01_LCD_CMD_COLMOD, (uint8_t[]){panel->state.colmod_val, 1}, 0);
    if(s != GC9A01_OK) { return s; }

    size_t init_cmds_size = sizeof(init_cmds_default)/sizeof(GC9A01_InitCmd);
    for (int i = 0; i < init_cmds_size; i++) {
        // Check if the command has been used or conflicts with the internal
        switch (init_cmds_default[i].cmd) {
        case GC9A01_LCD_CMD_MADCTL:
            panel->state.colmod_val = ((uint8_t *)(init_cmds_default[i].data))[0];
            break;
        case GC9A01_LCD_CMD_COLMOD:
            panel->state.colmod_val = ((uint8_t *)(init_cmds_default[i].data))[0];
            break;
        default:
            break;
        }

        s = GC9A01_TransmitParam(panel, init_cmds_default[i].cmd, init_cmds_default[i].data, init_cmds_default[i].data_bytes);
        if(s != GC9A01_OK) { return s; }
        hal->delay_ms(init_cmds_default[i].delay_ms);
    }

    return s;
}

/** 
 * @brief Destroy LCD panel.
 * @param[in] panel LCD panel handle.
 * @return `GC9A01_OK` on success.
 */
GC9A01_Status GC9A01_Destroy(GC9A01_Panel *panel) {
    if(!panel || !(panel->hal)) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    panel->hal->gpio_reset(panel->hal->RST);
    // free(gc9a01);
    return GC9A01_OK;
}

/**
 * @brief Draw bitmap on LCD panel.
 * @param[in] panel LCD panel handle.
 * @param[in] x_start Start pixel index in the target frame buffer, on x-axis (x_start is included).
 * @param[in] y_start Start pixel index in the target frame buffer, on y-axis (y_start is included).
 * @param[in] x_end End pixel index in the target frame buffer, on x-axis (x_end is not included).
 * @param[in] y_end End pixel index in the target frame buffer, on y-axis (y_end is not included).
 * @param[in] color_data RGB color data that will be dumped to the specific window range.
 * @return `GC9A01_OK` on success
 */
GC9A01_Status GC9A01_DrawBitmap(GC9A01_Panel *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data) {
    if((x_start < x_end) || (y_start < y_end)) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    GC9A01_Status s = GC9A01_OK;

    x_start += panel->config->x_gap;
    x_end   += panel->config->x_gap;
    y_start += panel->config->y_gap;
    y_end   += panel->config->y_gap;

    uint8_t raset_param[] = {
        (x_start >> 8)      & 0xFF,
        (x_start)           & 0xFF,
        ((x_end - 1) >> 8)  & 0xFF,
        (x_end - 1)         & 0xFF
    };
    s = GC9A01_TransmitParam(panel, GC9A01_LCD_CMD_CASET, raset_param, sizeof(raset_param)/sizeof(uint8_t));
    if(s != GC9A01_OK) { return s; }

    uint8_t caset_param[] = {
        (y_start >> 8)      & 0xFF,
        (y_start)           & 0xFF,
        ((y_end - 1) >> 8)  & 0xFF,
        (y_end - 1)         & 0xFF
    };
    s = GC9A01_TransmitParam(panel, GC9A01_LCD_CMD_CASET, caset_param, sizeof(caset_param)/sizeof(uint8_t));
    if(s != GC9A01_OK) { return s; }

    size_t color_size = ((x_end - x_start) * (y_end - y_start) * panel->config->fb_bits_per_pixels)/8;
    return GC9A01_TransmitColor(panel, GC9A01_LCD_CMD_RAMRD, color_data, color_size);
}

/**
 * @brief Mirror the LCD panel on specific axis.
 * @note Combine this function with `swap_xy`, one can realize screen rotatation.
 * @param[in] panel LCD panel handle.
 * @param[in] x_axis Whether the panel will be mirrored about the x_axis.
 * @param[in] y_axis Whether the panel will be mirrored about the y_axis.
 * @return `GC9A01_OK` on success.
 */
GC9A01_Status GC9A01_Mirror(GC9A01_Panel *panel, bool x_axis, bool y_axis) {
    if(x_axis) {
        panel->state.madctl_val |= GC9A01_LCD_CMD_MX_BIT;
    } else {
        panel->state.madctl_val &= ~GC9A01_LCD_CMD_MX_BIT;
    }

    if(y_axis) {
        panel->state.madctl_val |= GC9A01_LCD_CMD_MY_BIT;
    } else {
        panel->state.madctl_val &= ~GC9A01_LCD_CMD_MY_BIT;
    }

    uint8_t mirror_param = panel->state.madctl_val;
    return GC9A01_TransmitParam(panel, GC9A01_LCD_CMD_MADCTL, &mirror_param, sizeof(mirror_param)/sizeof(uint8_t));
}

/**
 * @brief Swap/Exchange x and y axis.
 * @note Combine this function with `mirror`, one can realize screen rotatation.
 * @param[in] panel LCD panel handle.
 * @param[in] swap_axes Whether to swap the x and y axis.
 * @return `GC9A01_OK` on success.
 */
GC9A01_Status GC9A01_SwapXY(GC9A01_Panel *panel, bool swap_axes) {
    if(swap_axes) {
        panel->state.madctl_val |= GC9A01_LCD_CMD_MV_BIT;
    } else {
        panel->state.madctl_val &= ~GC9A01_LCD_CMD_MV_BIT;
    }

    uint8_t swap_param = panel->state.madctl_val;
    return GC9A01_TransmitParam(panel, GC9A01_LCD_CMD_MADCTL, &swap_param, sizeof(swap_param)/sizeof(uint8_t));
}

/**
 * @brief Set extra gap in x and y axis.
 * @note The gap is only used for calculating the real coordinates.
 * @param[in] panel LCD panel handle.
 * @param[in] x_gap Extra gap on x axis, in pixels.
 * @param[in] y_gap Extra gap on y axis, in pixels.
 * @return `GC9A01_OK` on success.
 */
GC9A01_Status GC9A01_SetGap(GC9A01_Panel *panel, int x_gap, int y_gap) {
    if(!panel) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    panel->config->x_gap = x_gap;
    panel->config->y_gap = y_gap;
    return GC9A01_OK;
}

/**
 * @brief Invert the color (bit 1 -> 0 for color data line, and vice versa).
 * @param[in] panel LCD panel handle.
 * @param[in] invert_color_data Whether to invert the color data.
 * @return `GC9A01_OK` on success.
 */
GC9A01_Status GC9A01_InvertColor(GC9A01_Panel *panel, bool invert_color_data) {
    if(invert_color_data) {
        return GC9A01_TransmitParam(panel, GC9A01_LCD_CMD_INVON, NULL, 0);
    } else {
        return GC9A01_TransmitParam(panel, GC9A01_LCD_CMD_INVOFF, NULL, 0);
    }
}

/**
 * @brief Turn on or off the display.
 * @param[in] panel LCD panel handle.
 * @param[in] on_off True to turns on display, False to turns off display.
 * @return `GC9A01_OK` on success.
 */
GC9A01_Status GC9A01_DispOnOff(GC9A01_Panel *panel, bool on_off) {
    if(on_off) {
        return GC9A01_TransmitParam(panel, GC9A01_LCD_CMD_DISPON, NULL, 0);
    } else {
        return GC9A01_TransmitParam(panel, GC9A01_LCD_CMD_DISPOFF, NULL, 0);
    }
}

/**
 * @brief Enter or exit sleep mode.
 * @param[in] panel LCD panel handle.
 * @param[in] sleep True to enter sleep mode, False to wake up.
 * @return `GC9A01_OK` on success.
 */
GC9A01_Status GC9A01_DispSleep(GC9A01_Panel *panel, bool sleep) {
    /**
     * @ref p.101 datasheet at ./docs/GC9A01A.pdf.
     * This command has no effect when module is already in sleep in mode. Sleep In Mode can
     * only be left by the Sleep Out Command (11h). It will be necessary to wait 5msec before
     * sending next to command, this is to allow time for the supply voltages and clock circuits
     * to stabilize. It will be necessary to wait 120msec after sending Sleep Out command (when
     * in Sleep In Mode) before Sleep In command can be sent.
     */
    GC9A01_Status s = GC9A01_OK;
    if(sleep) {
        s = GC9A01_TransmitParam(panel, GC9A01_LCD_CMD_DISPON, NULL, 0);
    } else {
        s = GC9A01_TransmitParam(panel, GC9A01_LCD_CMD_DISPOFF, NULL, 0);
    }
    panel->hal->delay_ms(120);

    return s;
}


/* LAYER 3: APPLICATION */

static GC9A01_Status GC9A01_CreateDefaultPanel(GC9A01_Panel *panel) {
    panel->hal->gpio_reset                  = NULL;
    panel->hal->gpio_write                  = NULL;
    panel->hal->delay_ms                    = NULL;
    panel->hal->BLK.ctx                     = NULL;
    panel->hal->DC.ctx                      = NULL;
    panel->hal->RST.ctx                     = NULL;
    panel->hal->CS.ctx                      = NULL;
    panel->hal->BLK.pin                     = -1;
    panel->hal->DC.pin                      = -1;
    panel->hal->RST.pin                     = -1;
    panel->hal->CS.pin                      = -1;
    panel->hal->flags.dc_cmd_level          = 1;
    panel->hal->flags.dc_param_level        = 1;
    panel->hal->flags.rst_level             = 0;
    panel->hal->flags.cs_active_level       = 0;
    panel->hal->type                        = GC9A01_SPI_TRANSMIT_TYPE_POLLING;
    panel->hal->spi_trans_max_bytes         = 0;
    panel->hal->spi_ctx                     = NULL;
    panel->hal->spi_polling.spi_transmit    = NULL;
    panel->hal->spi_polling.spi_acquire_bus = NULL;
    panel->hal->spi_polling.spi_release_bus = NULL;
    panel->state.madctl_val                 = 0x00;
    panel->state.colmod_val                 = 0x00;
    panel->ctx                              = NULL;
    return GC9A01_OK;
}

GC9A01_Status GC9A01_CreatePanel(GC9A01_Panel *panel, GC9A01_Hal *hal, GC9A01_Config *config, void *ctx) {
    if(!hal) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    GC9A01_Status s = GC9A01_OK;
    s = GC9A01_CreateDefaultPanel(panel);
    if(s != GC9A01_OK) { return s; }

    panel->hal              = hal;
    panel->config           = config;
    panel->ctx              = ctx;

    panel->hal->gpio_reset(panel->hal->RST);

    switch(panel->config->rgb_element_order) {
        case GC9A01_RGB_ELEMENT_ORDER_RGB:
            panel->state.madctl_val &= ~GC9A01_LCD_CMD_BGR_BIT;
            break;
        
        case GC9A01_RGB_ELEMENT_ORDER_BGR:
            panel->state.madctl_val |= GC9A01_LCD_CMD_BGR_BIT;
            break;
        default:
            panel->hal->gpio_reset(panel->hal->RST);
            return GC9A01_ERROR_NOT_SUPPORTED;
    }

    switch(panel->config->bits_per_pixel) {
        case 16:
            panel->state.colmod_val           = 0x55;
            panel->config->fb_bits_per_pixels = 16;
            break;

        case 18:
            panel->state.colmod_val           = 0x66;
            panel->config->fb_bits_per_pixels = 24;
            break;

        default:
            panel->hal->gpio_reset(panel->hal->RST);
            return GC9A01_ERROR_NOT_SUPPORTED;
    }

    return GC9A01_OK;
}

GC9A01_Status GC9A01_HalSetGpio(GC9A01_Hal *hal, GC9A01_Gpio DC, GC9A01_Gpio RST, GC9A01_Gpio CS, GC9A01_Gpio BLK) {
    if(!hal) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    hal->DC  = DC;
    hal->RST = RST;
    hal->CS  = CS;
    hal->BLK = BLK;
    return GC9A01_OK;
}

GC9A01_Status GC9A01_HalSetLogicLevel(GC9A01_Hal *hal, bool dc_cmd_level, bool dc_param_level, bool cs_active_level, bool rst_level) {
    if(!hal) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    hal->flags.dc_cmd_level    = dc_cmd_level;
    hal->flags.dc_param_level  = dc_param_level;
    hal->flags.cs_active_level = cs_active_level;
    hal->flags.rst_level       = rst_level;
    return GC9A01_OK;
}

GC9A01_Status GC9A01_HalSetGpioApis(GC9A01_Hal *hal, GC9A01_GpioReset gpio_reset, GC9A01_GpioWrite gpio_write) {
    if(!hal || !gpio_reset || !gpio_write) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    hal->gpio_reset = gpio_reset;
    hal->gpio_write = gpio_write;
    return GC9A01_OK;
}

GC9A01_Status GC9A01_HalSetDelayMs(GC9A01_Hal *hal, GC9A01_DelayMs delay_ms) {
    if(!delay_ms) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    hal->delay_ms = delay_ms;
    return GC9A01_OK;
}

void GC9A01_HalSetTransmitType(GC9A01_Hal *hal, GC9A01_SpiTransmitType type) {
    hal->type = type;
}

GC9A01_Status GC9A01_HalSetSpiTransMaxBytes(GC9A01_Hal *hal, size_t spi_trans_max_bytes) {
    hal->spi_trans_max_bytes = spi_trans_max_bytes;
    return GC9A01_OK;
}

GC9A01_Status GC9A01_HalSetSpiCtx(GC9A01_Hal *hal, void *spi_ctx) {
    hal->spi_ctx = spi_ctx;
    return GC9A01_OK;
}

GC9A01_Status GC9A01_HalSetSpiTransmit(GC9A01_Hal *hal, GC9A01_SpiTransmit spi_transmit) {
    if(!spi_transmit) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    if(hal->type == GC9A01_SPI_TRANSMIT_TYPE_POLLING) {
        hal->spi_polling.spi_transmit = spi_transmit;
    } else if(hal->type == GC9A01_SPI_TRANSMIT_TYPE_ASYNC) {
        hal->spi_async.spi_transmit = spi_transmit;
    } else {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    return GC9A01_OK;
}

GC9A01_Status GC9A01_HalSetSpiTransmitAsync(GC9A01_Hal *hal, GC9A01_SpiTransmitAsync spi_transmit_async) {
    if(!spi_transmit_async || hal->type != GC9A01_SPI_TRANSMIT_TYPE_ASYNC) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    hal->spi_async.spi_transmit_async = spi_transmit_async;
    return GC9A01_OK;
}

GC9A01_Status GC9A01_HalSetSpiAcquireBus(GC9A01_Hal *hal, GC9A01_SpiAcquireBus spi_acquire_bus) {
    if(!spi_acquire_bus) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    if(hal->type == GC9A01_SPI_TRANSMIT_TYPE_POLLING) {
        hal->spi_polling.spi_acquire_bus = spi_acquire_bus;
    } else if(hal->type == GC9A01_SPI_TRANSMIT_TYPE_ASYNC) {
        hal->spi_async.spi_acquire_bus = spi_acquire_bus;
    } else {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    return GC9A01_OK;
}

GC9A01_Status GC9A01_HalSetSpiReleaseBus(GC9A01_Hal *hal, GC9A01_SpiReleaseBus spi_release_bus) {
    if(!spi_release_bus) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    if(hal->type == GC9A01_SPI_TRANSMIT_TYPE_POLLING) {
        hal->spi_polling.spi_release_bus = spi_release_bus;
    } else if(hal->type == GC9A01_SPI_TRANSMIT_TYPE_ASYNC) {
        hal->spi_async.spi_release_bus = spi_release_bus;
    } else {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    return GC9A01_OK;
}

GC9A01_Status GC9A01_HalSetQueueSize(GC9A01_Hal *hal, size_t queue_size) {
    if(hal->type != GC9A01_SPI_TRANSMIT_TYPE_ASYNC) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    hal->spi_async.queue_size = queue_size;
    return GC9A01_OK;
}

GC9A01_Status GC9A01_HalSetSpiGetTransResult(GC9A01_Hal *hal, GC9A01_SpiGetTransResult spi_get_trans_result) {
    if(!spi_get_trans_result || hal->type != GC9A01_SPI_TRANSMIT_TYPE_ASYNC) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    hal->spi_async.spi_get_trans_result = spi_get_trans_result;
    return GC9A01_OK;
}

GC9A01_Status GC9A01_HalSetSpiRegisterTransDoneCb(GC9A01_Hal *hal, GC9A01_SpiRegisterTransDoneCb register_spi_trans_done_cb) {
    if(!register_spi_trans_done_cb || hal->type != GC9A01_SPI_TRANSMIT_TYPE_ASYNC) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    hal->spi_async.register_spi_trans_done_cb = register_spi_trans_done_cb;
    return GC9A01_OK;
}
