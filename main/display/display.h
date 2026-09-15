#ifndef DISPLAY_H_
#define DISPLAY_H_

#include <stdio.h>
#include <stdlib.h>
#include <sys/lock.h>
#include <unistd.h>
#include "lvgl.h"
#include "gc9a01.h"
#include "_config.h"

extern lv_display_t *display;
extern _lock_t lvgl_api_lock;

int8_t display_init(void);
void lvgl_port_task(void *arg);

/** @brief Text demo. */
void lvgl_demo_text_ui(lv_display_t *disp);
/** @brief Digital clock demo. */
void lvgl_demo_clock_ui(lv_display_t *disp);
/** @brief Toggle switch demo. */
void lvgl_demo_toggle_switch_ui(lv_display_t *disp);
/** @brief Color cycle demo. */
void lvgl_demo_color_toggle_ui(lv_display_t *disp);
/** @brief Image demo. */
void lvgl_demo_image_ui(lv_display_t *disp);

#endif /* DISPLAY_H_ */