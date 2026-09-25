idf_component_register(SRCS "main.c" "node_thermal.c" "node_link.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES rb_config rb_log rb_node_sensors rb_espnow rb_protocol mlx90640 thermal_features c4002
                                     esp_timer esp_wifi)
