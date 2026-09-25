# Controller-side view of a sensor node: packet tracking (section 7) and link
# health (section 8). Pure C, unit-tested on the linux target.
idf_component_register(SRCS "src/sensor_node.c"
                       INCLUDE_DIRS "include"
                       REQUIRES rb_protocol rb_types)
