#ifndef GC9A01_PANEL_H_
#define GC9A01_PANEL_H_

#include "gc9a01_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 
 * @brief Reset LCD panel.
 * @param[in] panel LCD panel handle.
 * @return `GC9A01_OK` on success.
 */
GC9A01_Status GC9A01_Reset(GC9A01_Panel *panel);

/**
 * @brief Initialize LCD panel.
 * @param[in] panel LCD panel handle.
 * @return `GC9A01_OK` on success.
 */
GC9A01_Status GC9A01_Init(GC9A01_Panel *panel);

/** 
 * @brief Destroy LCD panel.
 * @param[in] panel LCD panel handle.
 * @return `GC9A01_OK` on success.
 */
GC9A01_Status GC9A01_Destroy(GC9A01_Panel *panel);

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
GC9A01_Status GC9A01_DrawBitmap(GC9A01_Panel *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data);

/**
 * @brief Mirror the LCD panel on specific axis.
 * @note Combine this function with `swap_xy`, one can realize screen rotatation.
 * @param[in] panel LCD panel handle.
 * @param[in] x_axis Whether the panel will be mirrored about the x_axis.
 * @param[in] y_axis Whether the panel will be mirrored about the y_axis.
 * @return `GC9A01_OK` on success.
 */
GC9A01_Status GC9A01_Mirror(GC9A01_Panel *panel, bool x_axis, bool y_axis);

/**
 * @brief Swap/Exchange x and y axis.
 * @note Combine this function with `mirror`, one can realize screen rotatation.
 * @param[in] panel LCD panel handle.
 * @param[in] swap_axes Whether to swap the x and y axis.
 * @return `GC9A01_OK` on success.
 */
GC9A01_Status GC9A01_SwapXY(GC9A01_Panel *panel, bool swap_axes);

/**
 * @brief Set extra gap in x and y axis.
 * @note The gap is only used for calculating the real coordinates.
 * @param[in] panel LCD panel handle.
 * @param[in] x_gap Extra gap on x axis, in pixels.
 * @param[in] y_gap Extra gap on y axis, in pixels.
 * @return `GC9A01_OK` on success.
 */
GC9A01_Status GC9A01_SetGap(GC9A01_Panel *panel, int x_gap, int y_gap);

/**
 * @brief Invert the color (bit 1 -> 0 for color data line, and vice versa).
 * @param[in] panel LCD panel handle.
 * @param[in] invert_color_data Whether to invert the color data.
 * @return `GC9A01_OK` on success.
 */
GC9A01_Status GC9A01_InvertColor(GC9A01_Panel *panel, bool invert_color_data);

/**
 * @brief Turn on or off the display.
 * @param[in] panel LCD panel handle.
 * @param[in] on_off True to turns on display, False to turns off display.
 * @return `GC9A01_OK` on success.
 */
GC9A01_Status GC9A01_DispOnOff(GC9A01_Panel *panel, bool on_off);

/**
 * @brief Enter or exit sleep mode.
 * @param[in] panel LCD panel handle.
 * @param[in] sleep True to enter sleep mode, False to wake up.
 * @return `GC9A01_OK` on success.
 */
GC9A01_Status GC9A01_DispSleep(GC9A01_Panel *panel, bool sleep);

#ifdef __cplusplus
}
#endif

#endif /* GC9A01_PANEL_H_ */