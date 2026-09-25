# Maps the state machine's outputs onto the buzzer and shutdown relay drivers.
idf_component_register(SRCS "src/rb_outputs.c"
                       INCLUDE_DIRS "include"
                       REQUIRES safety
                       PRIV_REQUIRES buzzer shutdown_output rb_config rb_time)
