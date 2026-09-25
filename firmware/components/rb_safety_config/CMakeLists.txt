# Builds a safety_config_t from menuconfig. Kept out of the safety component
# so the state machine itself has no Kconfig dependency.
idf_component_register(SRCS "src/rb_safety_config.c"
                       INCLUDE_DIRS "include"
                       REQUIRES safety
                       PRIV_REQUIRES rb_config)
