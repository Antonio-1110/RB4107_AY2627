idf_component_register(SRCS "test_main.c"
                            "test_safety.c"
                            "test_sensor_node.c"
                            "test_faults.c"
                            "test_protocol.c"
                            "test_c4002.c"
                            "test_thermal.c"
                            "test_json.c"
                            "test_kconfig.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES unity safety rb_safety_config sensor_node fault_manager rb_protocol c4002 thermal_features rb_json
                                     rb_topics rb_types)
