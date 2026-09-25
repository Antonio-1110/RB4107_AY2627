idf_component_register(SRCS "app_main.c" "continuity_monitor.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES rb_config rb_controller_app rb_controller sensor_node rb_mqtt fault_manager rb_time safety)
