#ifndef _CONFIG_H_
#define _CONFIG_H_
#include "driver/gpio.h"

/* GC9A01 pin map */
#define GC9A01_GPIO_BL          GPIO_NUM_14
#define GC9A01_GPIO_DC          GPIO_NUM_15
#define GC9A01_GPIO_RST         GPIO_NUM_16
#define GC9A01_GPIO_CS          GPIO_NUM_10
#define GC9A01_GPIO_SCK         GPIO_NUM_17
#define GC9A01_GPIO_MOSI        GPIO_NUM_11
#define GC9A01_GPIO_MISO        GPIO_NUM_NC

/* LVGL task / buffer tuning */
#define LVGL_DRAW_BUFFER_LINES  20   /* single source of truth, also used for SPI max-trans size */
#define LVGL_TICK_PERIOD_MS     2
#define LVGL_TASK_MAX_DELAY_MS  500
#define LVGL_TASK_MIN_DELAY_MS  (1000 / CONFIG_FREERTOS_HZ)
#define LVGL_TASK_STACK_SIZE    (8 * 1024)
#define LVGL_TASK_PRIORITY      2

#endif /* _CONFIG_H_ */