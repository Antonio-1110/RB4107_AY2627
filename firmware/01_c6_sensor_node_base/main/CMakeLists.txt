idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES rb_config esp_driver_gpio esp_timer nvs_flash esp_hw_support spi_flash)
