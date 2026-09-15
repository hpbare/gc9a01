#ifndef GC9A01_H_
#define GC9A01_H_

#include "gc9a01_types.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief GC9A01 panel handle. */
    typedef struct {
        GC9A01_Hal      *hal;       /**<! Hal abstract. */
        GC9A01_Config   *config;    /**<! Panel configurations. */
        GC9A01_Internal state;      /**<! Internal state. */
        void            *ctx;       /**<! User context. */
    } GC9A01_Panel;

    /* ===================== LAYER 3: APPLICATION ===================== */

    GC9A01_Status GC9A01_CreatePanel            (GC9A01_Panel *panel, GC9A01_Hal *hal, GC9A01_Config *config, void *ctx);

    GC9A01_Status GC9A01_CreateDefaultHal       (GC9A01_Hal *hal);
    GC9A01_Status GC9A01_HalSetGpio             (GC9A01_Hal *hal, GC9A01_Gpio DC, GC9A01_Gpio RST, GC9A01_Gpio CS, GC9A01_Gpio BLK);
    GC9A01_Status GC9A01_HalSetLogicLevel       (GC9A01_Hal *hal, bool dc_cmd_level, bool dc_param_level, bool cs_active_level, bool rst_level, bool bkl_on_level);
    GC9A01_Status GC9A01_HalSetGpioApis         (GC9A01_Hal *hal, GC9A01_GpioReset gpio_reset, GC9A01_GpioWrite gpio_write);
    GC9A01_Status GC9A01_HalSetDelayMs          (GC9A01_Hal *hal, GC9A01_DelayMs delay_ms);
    GC9A01_Status GC9A01_HalSetSpiTransMaxBytes (GC9A01_Hal *hal, size_t spi_trans_max_bytes);
    GC9A01_Status GC9A01_HalSetSpiCtx           (GC9A01_Hal *hal, void *spi_ctx);
    GC9A01_Status GC9A01_HalSetSpiTransmit      (GC9A01_Hal *hal, GC9A01_SpiTransmit spi_transmit);
    GC9A01_Status GC9A01_HalSetSpiAcquireBus    (GC9A01_Hal *hal, GC9A01_SpiAcquireBus spi_acquire_bus);
    GC9A01_Status GC9A01_HalSetSpiReleaseBus    (GC9A01_Hal *hal, GC9A01_SpiReleaseBus spi_release_bus);

    /* ===================== LAYER 2: PANEL ===================== */

    GC9A01_Status GC9A01_Reset                  (GC9A01_Panel *panel);
    GC9A01_Status GC9A01_Init                   (GC9A01_Panel *panel);
    GC9A01_Status GC9A01_Destroy                (GC9A01_Panel *panel);
    GC9A01_Status GC9A01_DrawBitmap             (GC9A01_Panel *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data);
    GC9A01_Status GC9A01_Mirror                 (GC9A01_Panel *panel, bool x_axis, bool y_axis);
    GC9A01_Status GC9A01_SwapXY                 (GC9A01_Panel *panel, bool swap_axes);
    GC9A01_Status GC9A01_SetGap                 (GC9A01_Panel *panel, int x_gap, int y_gap);
    GC9A01_Status GC9A01_InvertColor            (GC9A01_Panel *panel, bool invert_color_data);
    GC9A01_Status GC9A01_DispOnOff              (GC9A01_Panel *panel, bool on_off);
    GC9A01_Status GC9A01_BacklightOnOff         (GC9A01_Panel *panel, bool on_off);
    GC9A01_Status GC9A01_DispSleep              (GC9A01_Panel *panel, bool sleep);

#ifdef __cplusplus
}
#endif

#endif /* GC9A01_H_ */