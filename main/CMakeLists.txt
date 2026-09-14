idf_component_register(
    SRCS
        # "main_esp_lcd.c"            # Test with esp_lcd architecture
        # "main_gc9a01.c"
        "main_gc9a01_blocking.c"    # Test blocking branch
        # "main_gc9a01_async.c"       # Test async branch
        # "main_lvgl_watchface_demo.c"  # Test LVGL watch face demo
            "./images/logo.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        lvgl
        driver
        esp_timer
        gc9a01
)
