# Controller runtime (TODO section 12): the ESP-NOW receive queue, the
# high-priority safety task, and the event queue and snapshot read by the
# lower-priority tasks.
idf_component_register(SRCS "src/rb_controller.c"
                       INCLUDE_DIRS "include"
                       REQUIRES safety sensor_node rb_espnow rb_protocol freertos
                       PRIV_REQUIRES rb_config rb_safety_config rb_time rb_log)
