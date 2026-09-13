idf_component_register(
    SRCS
        # "main_esp_lcd.c"
        # "main_gc9a01.c"
        "main_gc9a01_blocking.c"    # Test blocking branch
        # "main_gc9a01_async.c"       # Test async branch
    INCLUDE_DIRS
        "."
    REQUIRES
        lvgl
        driver
        esp_timer
        gc9a01
)
