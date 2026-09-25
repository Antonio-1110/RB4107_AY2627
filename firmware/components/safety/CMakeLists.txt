# Cooking-safety state machine. Pure C with no hardware, RTOS or MQTT
# dependency, so it can be unit tested (project 28) and simulated (09-11, 27).
idf_component_register(SRCS "src/safety.c"
                       INCLUDE_DIRS "include"
                       REQUIRES rb_types)
