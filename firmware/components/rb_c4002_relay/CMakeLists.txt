# Remote C4002 tuning, controller side: MQTT command -> ESP-NOW C4002_CONFIG
# -> presence node, and the node's ACK back to MQTT (docs/c4002_tuning.md).
idf_component_register(SRCS "src/rb_c4002_relay.c"
                       INCLUDE_DIRS "include"
                       REQUIRES rb_espnow rb_protocol rb_c4002_cmd
                       PRIV_REQUIRES rb_config rb_mqtt rb_topics rb_time esp_timer freertos)
