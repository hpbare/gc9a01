#ifndef GC9A01_IO_H
#define GC9A01_IO_H

#include "gc9a01_types.h"
#include "gc9a01_cmds.h"

GC9A01_Status GC9A01_TransmitParam(GC9A01_Panel *panel, GC9A01_SpiCmds cmd, const void *param, size_t param_size);
GC9A01_Status GC9A01_TransmitColor(GC9A01_Panel *panel, GC9A01_SpiCmds cmd, const void *color, size_t color_size);
/** 
 * @brief Call this function on DMA callback to release CS pin.
 * @param panel GC9A01 panel handle.
 */
void GC9A01_OnTransactionDone(GC9A01_Panel *panel);

#endif /* GC9A01_IO_H */