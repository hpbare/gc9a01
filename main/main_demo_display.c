#include <stdio.h>
#include "display/display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void) {
    if (display_init() != 0) {
        return;
    }

    xTaskCreate(lvgl_port_task, "LVGL_TASK", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL);
    // lvgl_demo_clock_ui(display);
    lvgl_demo_image_ui(display);
}