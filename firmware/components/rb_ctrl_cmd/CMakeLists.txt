# Dashboard -> controller commands (operator reset): parses the MQTT command
# JSON. Pure C: unit-tested on the linux target.
idf_component_register(SRCS "src/rb_ctrl_cmd.c"
                       INCLUDE_DIRS "include")
