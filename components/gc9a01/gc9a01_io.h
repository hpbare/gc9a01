#ifndef GC9A01_IO_H_
#define GC9A01_IO_H_

#include "gc9a01.h"
#include "gc9a01_types.h"
#include "gc9a01_cmds.h"

GC9A01_Status GC9A01_TransmitParam(GC9A01_Panel *panel, GC9A01_LcdCmds cmd, const void *param, size_t param_size);
GC9A01_Status GC9A01_TransmitColor(GC9A01_Panel *panel, GC9A01_LcdCmds cmd, const void *color, size_t color_size);

#endif /* GC9A01_IO_H_ */