#ifndef GC9A01_CMDS_H_
#define GC9A01_CMDS_H_

/** @brief Common LCD panel commands */
typedef enum {
    GC9A01_LCD_CMD_NONE         = -1,
    GC9A01_LCD_CMD_NOP          = 0x00, /* This command is empty command. */
    GC9A01_LCD_CMD_SWRESET      = 0x01, /* Software reset registers (the built-in frame buffer is not affected). */
    GC9A01_LCD_CMD_RDDID        = 0x04, /* Read 24-bit display ID. */
    GC9A01_LCD_CMD_RDDST        = 0x09, /* Read display status. */
    GC9A01_LCD_CMD_RDDPM        = 0x0A, /* Read display power mode. */
    GC9A01_LCD_CMD_RDD_MADCTL   = 0x0B, /* Read display MADCTL. */
    GC9A01_LCD_CMD_RDD_COLMOD   = 0x0C, /* Read display pixel format. */
    GC9A01_LCD_CMD_RDDIM        = 0x0D, /* Read display image mode. */
    GC9A01_LCD_CMD_RDDSM        = 0x0E, /* Read display signal mode. */
    GC9A01_LCD_CMD_RDDSR        = 0x0F, /* Read display self-diagnostic result. */
    GC9A01_LCD_CMD_SLPIN        = 0x10, /* Go into sleep mode (DC/DC, oscillator, scanning stopped, but memory keeps content). */
    GC9A01_LCD_CMD_SLPOUT       = 0x11, /* Exit sleep mode. */
    GC9A01_LCD_CMD_PTLON        = 0x12, /* Turns on partial display mode. */
    GC9A01_LCD_CMD_NORON        = 0x13, /* Turns on normal display mode. */
    GC9A01_LCD_CMD_INVOFF       = 0x20, /* Recover from display inversion mode. */
    GC9A01_LCD_CMD_INVON        = 0x21, /* Go into display inversion mode. */
    GC9A01_LCD_CMD_GAMSET       = 0x26, /* Select Gamma curve for current display. */
    GC9A01_LCD_CMD_DISPOFF      = 0x28, /* Display off (disable frame buffer output). */
    GC9A01_LCD_CMD_DISPON       = 0x29, /* Display on (enable frame buffer output). */
    GC9A01_LCD_CMD_CASET        = 0x2A, /* Set column address. */
    GC9A01_LCD_CMD_RASET        = 0x2B, /* Set row address. */
    GC9A01_LCD_CMD_RAMWR        = 0x2C, /* Write frame memory. */
    GC9A01_LCD_CMD_RAMRD        = 0x2E, /* Read frame memory. */
    GC9A01_LCD_CMD_PTLAR        = 0x30, /* Define the partial area. */
    GC9A01_LCD_CMD_VSCRDEF      = 0x33, /* Vertical scrolling definition. */
    GC9A01_LCD_CMD_TEOFF        = 0x34, /* Turns off tearing effect. */
    GC9A01_LCD_CMD_TEON         = 0x35, /* Turns on tearing effect. */
    GC9A01_LCD_CMD_MADCTL       = 0x36, /* Memory data access control. */
    GC9A01_LCD_CMD_VSCSAD       = 0x37, /* Vertical scroll start address. */
    GC9A01_LCD_CMD_IDMOFF       = 0x38, /* Recover from IDLE mode. */
    GC9A01_LCD_CMD_IDMON        = 0x39, /* Fall into IDLE mode (8 color depth is displayed). */
    GC9A01_LCD_CMD_COLMOD       = 0x3A, /* Defines the format of RGB picture data. */
    GC9A01_LCD_CMD_RAMWRC       = 0x3C, /* Memory write continue. */
    GC9A01_LCD_CMD_RAMRDC       = 0x3E, /* Memory read continue. */
    GC9A01_LCD_CMD_STE          = 0x44, /* Set tear scan line, tearing effect output signal when display module reaches line N. */
    GC9A01_LCD_CMD_GDCAN        = 0x45, /* Get scan line. */
    GC9A01_LCD_CMD_WRDISBV      = 0x51, /* Write display brightness. */
    GC9A01_LCD_CMD_RDDISBV      = 0x52  /* Read display brightness value. */
} GC9A01_LcdCmds;


/** @brief MADCTL register. @ref p.127 datasheet at ./docs/GC9A01A.pdf */
#define GC9A01_LCD_CMD_MH_BIT   (1 << 2) // Horizontal Refresh ORDER, 0: refresh left to right, 1: refresh right to left
#define GC9A01_LCD_CMD_BGR_BIT  (1 << 3) // RGB/BGR order,            0: RGB,                   1: BGR
#define GC9A01_LCD_CMD_ML_BIT   (1 << 4) // Vertical Refresh Order,   0: refresh top to bottom, 1: refresh bottom to top
#define GC9A01_LCD_CMD_MV_BIT   (1 << 5) // Row/Column order,         0: normal mode,           1: reverse mode
#define GC9A01_LCD_CMD_MX_BIT   (1 << 6) // Column Address Order,     0: left to right,         1: right to left
#define GC9A01_LCD_CMD_MY_BIT   (1 << 7) // Row Address Order,        0: top to bottom,         1: bottom to top


#endif /* GC9A01_CMDS_H_ */