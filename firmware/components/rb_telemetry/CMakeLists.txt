# Telemetry task (TODO section 22): turns controller snapshots and events
# into MQTT messages. Low priority; only this task ever calls rb_mqtt.
idf_component_register(SRCS "src/rb_telemetry.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES rb_controller rb_mqtt rb_json rb_topics fault_manager rb_wallclock rb_time
                                     sensor_node safety rb_protocol rb_config freertos)
