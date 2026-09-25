# Sensor-node glue: turns the Kconfig values into driver configurations and
# brings the sensors up, so every C6 project starts them the same way.
idf_component_register(SRCS "src/rb_node_sensors.c"
                       INCLUDE_DIRS "include"
                       REQUIRES c4002 rb_types
                       PRIV_REQUIRES rb_config)
