# Logging conventions (TODO section 25): per-tag verbosity from menuconfig
# and a rate-limit helper for high-frequency paths.
idf_component_register(SRCS "src/rb_log.c"
                       INCLUDE_DIRS "include"
                       REQUIRES log
                       PRIV_REQUIRES rb_config)
