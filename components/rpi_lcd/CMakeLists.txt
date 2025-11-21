idf_component_register(
    SRCS "rpi_lcd.c"
    INCLUDE_DIRS
        "include"
        "$ENV{IDF_PATH}/components/esp_driver_spi/include"
        "$ENV{IDF_PATH}/components/esp_driver_gpio/include"
)
