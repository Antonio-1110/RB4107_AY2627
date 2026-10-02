idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES rb_config rb_log rb_node_app rb_node_sensors rb_protocol c4002 rb_c4002_remote)
