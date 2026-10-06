idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES rb_config rb_log rb_protocol rb_espnow rb_time servo valve_logic
                                     esp_driver_gpio esp_hw_support freertos)
