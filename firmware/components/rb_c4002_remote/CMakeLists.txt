# Presence node side of remote C4002 tuning: receives C4002_CONFIG from the
# controller over ESP-NOW, applies and saves the settings, answers with
# C4002_CONFIG_ACK (docs/c4002_tuning.md).
idf_component_register(SRCS "src/rb_c4002_remote.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES rb_config rb_protocol rb_espnow rb_node_app rb_node_sensors c4002 esp_timer freertos)
