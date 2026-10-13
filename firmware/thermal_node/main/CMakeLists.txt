idf_component_register(SRCS "main.c" "node_thermal.c" "node_thermal_remote.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES rb_config rb_log rb_node_app rb_node_sensors rb_protocol mlx90640 thermal_features thermal_frame
                                     esp_timer rb_espnow nvs_flash freertos)
