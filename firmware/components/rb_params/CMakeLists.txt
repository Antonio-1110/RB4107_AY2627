# The controller's other safety settings the dashboard can change (include/rb_params.def): their
# ranges, and reading and applying them. Pure C: unit-tested on the linux target.
idf_component_register(SRCS "src/rb_params.c"
                       INCLUDE_DIRS "include"
                       REQUIRES safety sensor_node)
