idf_component_register(SRCS "src/rb_espnow.c"
                       INCLUDE_DIRS "include"
                       REQUIRES rb_protocol esp_wifi freertos
                       PRIV_REQUIRES rb_wifi rb_log esp_timer nvs_flash)
