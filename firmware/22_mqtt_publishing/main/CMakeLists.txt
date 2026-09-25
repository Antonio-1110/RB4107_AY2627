idf_component_register(SRCS "app_main.c"
                       PRIV_REQUIRES rb_config rb_log rb_controller rb_app rb_outputs rb_espnow rb_sim rb_telemetry
                                     rb_connectivity rb_wallclock fault_manager)
