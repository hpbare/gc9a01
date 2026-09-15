#include "display.h"

/* Reference demo UIs. Only lvgl_demo_clock_ui is wired into app_main by
 * default (see main.c); the others are kept here for reference/testing. */

/* ---- Text demo ---- */

void lvgl_demo_text_ui(lv_display_t *disp) {
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(label, "Hello Espressif, Hello LVGL.");
    lv_obj_set_width(label, lv_display_get_horizontal_resolution(disp));
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);
}

/* ---- Digital clock demo ---- */

static lv_obj_t *time_label = NULL;

/* Fake time source: increments once per second from 00:00:00.
 * Swap this struct + clock_tick_cb body for a real RTC/NTP read later -
 * the label update / redraw path stays the same. */
static struct {
    uint8_t h, m, s;
} fake_time = { 0, 0, 0 };

static void clock_tick_cb(lv_timer_t *timer) {
    fake_time.s++;
    if (fake_time.s >= 60) { fake_time.s = 0; fake_time.m++; }
    if (fake_time.m >= 60) { fake_time.m = 0; fake_time.h++; }
    if (fake_time.h >= 24) { fake_time.h = 0; }

    char buf[16]; /* "HH:MM:SS\0" */
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", fake_time.h, fake_time.m, fake_time.s);
    lv_label_set_text(time_label, buf);
}

void lvgl_demo_clock_ui(lv_display_t *disp) {
    _lock_acquire(&lvgl_api_lock);
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    time_label = lv_label_create(scr);
    /* Montserrat 36 must be enabled: menuconfig -> Component config ->
     * LVGL -> Font usage -> Enable Montserrat 36 (CONFIG_LV_FONT_MONTSERRAT_36) */
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(time_label, lv_color_white(), 0);
    lv_label_set_text(time_label, "00:00:00");
    lv_obj_center(time_label); /* horizontal row through screen center is the widest chord on a round panel */

    /* 1-second fake tick. Runs inside lv_timer_handler(), which lvgl_port_task
     * already calls under lvgl_api_lock, so no extra locking is needed here. */
    lv_timer_create(clock_tick_cb, 1000, NULL);
    _lock_release(&lvgl_api_lock);
}

/* ---- Toggle switch demo ---- */

static void toggle_switch_event_cb(lv_event_t *e) {
    lv_obj_t *sw = lv_event_get_target(e);
    bool is_on = lv_obj_has_state(sw, LV_STATE_CHECKED);

    lv_obj_t *scr = lv_obj_get_parent(sw);
    lv_obj_set_style_bg_color(scr, is_on ? lv_color_white() : lv_color_black(), 0);

    lv_obj_t *status_label = (lv_obj_t *)lv_event_get_user_data(e);
    lv_label_set_text(status_label, is_on ? "ON" : "OFF");
    lv_obj_set_style_text_color(status_label, is_on ? lv_color_black() : lv_color_white(), 0);
}

void lvgl_demo_toggle_switch_ui(lv_display_t *disp) {
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *status_label = lv_label_create(scr);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(status_label, lv_color_white(), 0);
    lv_label_set_text(status_label, "OFF");
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *sw = lv_switch_create(scr);
    lv_obj_align(sw, LV_ALIGN_CENTER, 0, 30);
    /* status_label passed as user_data so the callback can update it directly,
     * avoiding a second static global like time_label above. */
    lv_obj_add_event_cb(sw, toggle_switch_event_cb, LV_EVENT_VALUE_CHANGED, status_label);
}

/* ---- Color cycle demo ---- */

static lv_obj_t *color_rect = NULL;

static const lv_color_t color_cycle[] = {
    LV_COLOR_MAKE(0xFF, 0x00, 0x00), /* red */
    LV_COLOR_MAKE(0x00, 0xFF, 0x00), /* green */
    LV_COLOR_MAKE(0x00, 0x00, 0xFF), /* blue */
    LV_COLOR_MAKE(0xFF, 0xFF, 0xFF), /* white */
};
#define COLOR_CYCLE_COUNT (sizeof(color_cycle) / sizeof(color_cycle[0]))

static void color_cycle_tick_cb(lv_timer_t *timer) {
    static uint8_t idx = 0;
    lv_obj_set_style_bg_color(color_rect, color_cycle[idx], 0);
    idx = (idx + 1) % COLOR_CYCLE_COUNT;
}

void lvgl_demo_color_toggle_ui(lv_display_t *disp) {
    lv_obj_t *scr = lv_display_get_screen_active(disp);

    color_rect = lv_obj_create(scr);
    lv_obj_remove_style_all(color_rect); /* strip default border/padding/scrollbar */
    lv_obj_set_size(color_rect, lv_display_get_horizontal_resolution(disp),
                                 lv_display_get_vertical_resolution(disp));
    lv_obj_set_pos(color_rect, 0, 0);
    lv_obj_set_style_bg_opa(color_rect, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(color_rect, color_cycle[0], 0);

    /* 500ms period, matches vTaskDelay(pdMS_TO_TICKS(500)) in the original raw-driver loop */
    lv_timer_create(color_cycle_tick_cb, 500, NULL);
}


/* ---- Image demo ---- */
LV_IMAGE_DECLARE(logo);

void lvgl_demo_image_ui(lv_display_t *disp)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *img = lv_image_create(scr);
    lv_image_set_src(img, &logo);   /* logo: lv_image_dsc_t generated by the image converter */
    lv_obj_center(img);
}