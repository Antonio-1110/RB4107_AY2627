# Telemetry-path bring-up: network -> SNTP -> MQTT, with the network/MQTT
# telemetry faults. Nothing on the safety path depends on it.
idf_component_register(SRCS "src/rb_connectivity.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES rb_net rb_mqtt rb_json rb_topics rb_wallclock fault_manager rb_config)
