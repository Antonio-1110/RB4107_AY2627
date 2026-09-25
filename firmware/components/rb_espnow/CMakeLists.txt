idf_component_register(SRCS "src/rb_espnow.c"
                       INCLUDE_DIRS "include"
                       REQUIRES rb_protocol esp_wifi
                       PRIV_REQUIRES rb_wifi esp_timer)
