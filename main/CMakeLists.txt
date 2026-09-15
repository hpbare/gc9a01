idf_component_register(
    SRCS
        "main_demo_display.c"                # Simplify code
        # "main_esp_lcd.c"                # Test with esp_lcd architecture
        # "main_gc9a01.c"
        # "main_gc9a01_blocking.c"        # Test blocking branch
        # "main_gc9a01_async.c"           # Test async branch
        # "main_lvgl_watchface_demo.c"    # Test LVGL watch face demo
            "./display/images/logo.c"
            "./display/display.c"
            "./display/display_demo.c"
    INCLUDE_DIRS
        "." "display"
    REQUIRES
        lvgl
        driver
        esp_timer
        gc9a01
)
