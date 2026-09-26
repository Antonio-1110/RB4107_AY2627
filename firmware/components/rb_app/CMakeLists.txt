# Controller application glue, built up section by section: fault wiring
# (16), network/MQTT/telemetry (17-22), and the complete controller (29).
idf_component_register(SRCS "src/rb_app_faults.c"
                       INCLUDE_DIRS "include"
                       REQUIRES rb_controller fault_manager sensor_node safety
                       PRIV_REQUIRES rb_espnow rb_outputs rb_time)
