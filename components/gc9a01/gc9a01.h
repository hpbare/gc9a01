#ifndef GC9A01_H_
#define GC9A01_H_

#include "gc9a01_panel.h"
#include "gc9a01_io.h"
#include "gc9a01_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    GC9A01_Status GC9A01_CreatePanel                 (GC9A01_Panel *panel, GC9A01_Hal *hal, void *ctx);

    GC9A01_Status GC9A01_HalSetGpio                  (GC9A01_Hal *hal, GC9A01_Gpio DC, GC9A01_Gpio RST, GC9A01_Gpio CS, GC9A01_Gpio BLK);
    GC9A01_Status GC9A01_HalSetLogicLevel            (GC9A01_Hal *hal, bool dc_cmd_level, bool dc_param_level, bool cs_active_level, bool rst_level);
    GC9A01_Status GC9A01_HalSetGpioApis              (GC9A01_Hal *hal, GC9A01_GpioReset *gpio_reset, GC9A01_GpioWrite *gpio_write);
    GC9A01_Status GC9A01_HalSetDelayMs               (GC9A01_Hal *hal, GC9A01_DelayMs *delay_ms);
    GC9A01_Status GC9A01_HalSetTransmitType          (GC9A01_Hal *hal, GC9A01_SpiTransmitType type);
    GC9A01_Status GC9A01_HalSetSpiTransMaxBytes      (GC9A01_Hal *hal, size_t spi_trans_max_bytes);
    GC9A01_Status GC9A01_HalSetSpiCtx                (GC9A01_Hal *hal, void *spi_ctx);
    GC9A01_Status GC9A01_HalSetSpiTransmit           (GC9A01_Hal *hal, GC9A01_SpiTransmit *spi_transmit);
    GC9A01_Status GC9A01_HalSetSpiTransmitAsync      (GC9A01_Hal *hal, GC9A01_SpiTransmitAsync *spi_transmit_async);
    GC9A01_Status GC9A01_HalSetSpiAcquireBus         (GC9A01_Hal *hal, GC9A01_SpiAcquireBus *spi_acquire_bus);
    GC9A01_Status GC9A01_HalSetSpiReleaseBus         (GC9A01_Hal *hal, GC9A01_SpiAcquireBus *spi_release_bus);
    GC9A01_Status GC9A01_HalSetQueueSize             (GC9A01_Hal *hal, size_t queue_size);
    GC9A01_Status GC9A01_HalSetSpiGetTransResult     (GC9A01_Hal *hal, GC9A01_SpiGetTransResult *spi_get_trans_result);
    GC9A01_Status GC9A01_HalSetSpiRegisterTransDoneCb(GC9A01_Hal *hal, GC9A01_SpiRegisterTransDoneCb *register_spi_trans_done_cb);

#ifdef __cplusplus
}
#endif

#endif /* GC9A01_H_ */