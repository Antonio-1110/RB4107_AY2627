# Code shared by the two C6 node firmwares (05a presence, 05b thermal): the
# board checks (section 1) and the ESP-NOW link task (section 5).
idf_component_register(SRCS "src/rb_node_board.c" "src/rb_node_link.c"
                       INCLUDE_DIRS "include"
                       REQUIRES rb_protocol esp_common
                       PRIV_REQUIRES rb_config rb_log rb_espnow esp_timer esp_driver_gpio esp_hw_support freertos)
