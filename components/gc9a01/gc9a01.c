#include "gc9a01.h"
#include "gc9a01_io.h"
#include "gc9a01_types.h"

static const GC9A01_InitCmd vendor_specific_init_default[] = {
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

GC9A01_Status GC9A01_Init(GC9A01_Panel *panel) {
    if(!panel || !(panel->hal)) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    GC9A01_Status s = GC9A01_OK;
    GC9A01_Hal *hal = panel->hal;

    s = GC9A01_TransmitParam(panel, GC9A01_LCD_CMD_SLPIN, NULL, 0);
    if(s != GC9A01_OK) { return s; }
    hal->delay_ms(100);

    s = GC9A01_TransmitParam(panel, GC9A01_LCD_CMD_MADCTL, (uint8_t[]){panel->madctl_val, 1}, 0);
    if(s != GC9A01_OK) { return s; }

    s = GC9A01_TransmitParam(panel, GC9A01_LCD_CMD_COLMOD, (uint8_t[]){panel->colmod_val, 1}, 0);
    if(s != GC9A01_OK) { return s; }

    if(panel->init_cmds == NULL){
        panel->init_cmds = vendor_specific_init_default;
        panel->init_cmds_size = sizeof(vendor_specific_init_default)/sizeof(GC9A01_InitCmd);
    }

    for (int i = 0; i < panel->init_cmds_size; i++) {
        // Check if the command has been used or conflicts with the internal
        switch (panel->init_cmds[i].cmd) {
        case GC9A01_LCD_CMD_MADCTL:
            panel->madctl_val = ((uint8_t *)(panel->init_cmds[i].data))[0];
            break;
        case GC9A01_LCD_CMD_COLMOD:
            panel->colmod_val = ((uint8_t *)(panel->init_cmds[i].data))[0];
            break;
        default:
            break;
        }

        s = GC9A01_TransmitParam(panel, panel->init_cmds[i].cmd, panel->init_cmds[i].data, panel->init_cmds[i].data_bytes);
        if(s != GC9A01_OK) { return s; }
        hal->delay_ms(panel->init_cmds[i].delay_ms);
    }

    return s;
}

GC9A01_Status GC9A01_Destroy(GC9A01_Panel *panel) {
    if(!panel || !(panel->hal)) {
        return GC9A01_ERROR_INVALID_ARGS;
    }
    panel->hal->gpio_reset(panel->hal->RST);
    // free(gc9a01);
    return GC9A01_OK;
}

GC9A01_Status GC9A01_DrawBitmap(GC9A01_Panel *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data) {

    return GC9A01_OK;
}

GC9A01_Status GC9A01_Mirror(GC9A01_Panel *panel, bool x_axis, bool y_axis) {

    return GC9A01_OK;
}

GC9A01_Status GC9A01_SwapXY(GC9A01_Panel *panel, bool swap_axes) {

    return GC9A01_OK;
}

GC9A01_Status GC9A01_SetGap(GC9A01_Panel *panel, int x_gap, int y_gap) {

    return GC9A01_OK;
}

GC9A01_Status GC9A01_InvertColor(GC9A01_Panel *panel, bool invert_color_data) {

    return GC9A01_OK;
}

GC9A01_Status GC9A01_DispOnOff(GC9A01_Panel *panel, bool on_off) {

    return GC9A01_OK;
}

GC9A01_Status GC9A01_DispSleep(GC9A01_Panel *panel, bool sleep) {

    return GC9A01_OK;
}
