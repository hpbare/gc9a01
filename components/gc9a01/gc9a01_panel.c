#include "gc9a01.h"
#include "gc9a01_cmds.h"

typedef struct {
    int cmd;
    const void *data;
    size_t data_bytes;
    unsigned int delay_ms;
} GC9A01_InitCmd;

static const GC9A01_InitCmd init_cmds_default[] = {
//  {cmd, { data }, data_size, delay_ms}
    // Enable Inter Register
    {0xfe, (uint8_t []){0x00}, 0, 0},
    {0xef, (uint8_t []){0x00}, 0, 0},
    {0xeb, (uint8_t []){0x14}, 1, 0},
    {0x84, (uint8_t []){0x60}, 1, 0},
    {0x85, (uint8_t []){0xff}, 1, 0},
    {0x86, (uint8_t []){0xff}, 1, 0},
    {0x87, (uint8_t []){0xff}, 1, 0},
    {0x8e, (uint8_t []){0xff}, 1, 0},
    {0x8f, (uint8_t []){0xff}, 1, 0},
    {0x88, (uint8_t []){0x0a}, 1, 0},
    {0x89, (uint8_t []){0x23}, 1, 0},
    {0x8a, (uint8_t []){0x00}, 1, 0},
    {0x8b, (uint8_t []){0x80}, 1, 0},
    {0x8c, (uint8_t []){0x01}, 1, 0},
    {0x8d, (uint8_t []){0x03}, 1, 0},
    {0x90, (uint8_t []){0x08, 0x08, 0x08, 0x08}, 4, 0},
    {0xff, (uint8_t []){0x60, 0x01, 0x04}, 3, 0},
    {0xC3, (uint8_t []){0x13}, 1, 0},
    {0xC4, (uint8_t []){0x13}, 1, 0},
    {0xC9, (uint8_t []){0x30}, 1, 0},
    {0xbe, (uint8_t []){0x11}, 1, 0},
    {0xe1, (uint8_t []){0x10, 0x0e}, 2, 0},
    {0xdf, (uint8_t []){0x21, 0x0c, 0x02}, 3, 0},
    // Set gamma
    {0xF0, (uint8_t []){0x45, 0x09, 0x08, 0x08, 0x26, 0x2a}, 6, 0},
    {0xF1, (uint8_t []){0x43, 0x70, 0x72, 0x36, 0x37, 0x6f}, 6, 0},
    {0xF2, (uint8_t []){0x45, 0x09, 0x08, 0x08, 0x26, 0x2a}, 6, 0},
    {0xF3, (uint8_t []){0x43, 0x70, 0x72, 0x36, 0x37, 0x6f}, 6, 0},
    {0xed, (uint8_t []){0x1b, 0x0b}, 2, 0},
    {0xae, (uint8_t []){0x77}, 1, 0},
    {0xcd, (uint8_t []){0x63}, 1, 0},
    {0x70, (uint8_t []){0x07, 0x07, 0x04, 0x0e, 0x0f, 0x09, 0x07, 0x08, 0x03}, 9, 0},
    {0xE8, (uint8_t []){0x34}, 1, 0}, // 4 dot inversion
    {0x60, (uint8_t []){0x38, 0x0b, 0x6D, 0x6D, 0x39, 0xf0, 0x6D, 0x6D}, 8, 0},
    {0x61, (uint8_t []){0x38, 0xf4, 0x6D, 0x6D, 0x38, 0xf7, 0x6D, 0x6D}, 8, 0},
    {0x62, (uint8_t []){0x38, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x38, 0x0F, 0x71, 0xEF, 0x70, 0x70}, 12, 0},
    {0x63, (uint8_t []){0x38, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x38, 0x13, 0x71, 0xF3, 0x70, 0x70}, 12, 0},
    {0x64, (uint8_t []){0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07}, 7, 0},
    {0x66, (uint8_t []){0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00}, 10, 0},
    {0x67, (uint8_t []){0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98}, 10, 0},
    {0x74, (uint8_t []){0x10, 0x45, 0x80, 0x00, 0x00, 0x4E, 0x00}, 7, 0},
    {0x98, (uint8_t []){0x3e, 0x07}, 2, 0},
    {0x99, (uint8_t []){0x3e, 0x07}, 2, 0},
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
