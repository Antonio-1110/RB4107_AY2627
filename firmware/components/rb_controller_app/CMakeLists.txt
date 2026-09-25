# The complete ESP32-S3 controller application (TODO sections 27, 29, 30):
# every component wired together, in one place.
idf_component_register(SRCS "src/rb_controller_app.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES rb_config rb_log rb_controller rb_app rb_outputs rb_espnow rb_sim rb_telemetry
                                     rb_connectivity rb_wallclock fault_manager rb_diag safety)
