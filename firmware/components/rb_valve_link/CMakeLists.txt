# Controller side of the wireless valve node: sends VALVE_COMMAND over
# ESP-NOW and keeps the latest VALVE_STATUS.
idf_component_register(SRCS "src/rb_valve_link.c"
                       INCLUDE_DIRS "include"
                       REQUIRES rb_protocol esp_common
                       PRIV_REQUIRES rb_espnow rb_config rb_time rb_log freertos)
