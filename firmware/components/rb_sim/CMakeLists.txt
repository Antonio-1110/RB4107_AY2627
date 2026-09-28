# Simulated sensor nodes (diagnostics / QEMU): injects protocol packets into
# the controller exactly where ESP-NOW would.
idf_component_register(SRCS "src/rb_sim.c"
                       INCLUDE_DIRS "include"
                       REQUIRES rb_types
                       PRIV_REQUIRES rb_controller rb_protocol rb_time freertos log)
