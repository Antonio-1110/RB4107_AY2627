# Diagnostic console (TODO section 27) and the critical-failure continuity
# monitor (TODO section 30).
idf_component_register(SRCS "src/rb_diag.c" "src/rb_continuity.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES console rb_controller rb_safety_config rb_sim rb_espnow rb_mqtt rb_net rb_telemetry
                                     fault_manager rb_config rb_time log sensor_node safety)
