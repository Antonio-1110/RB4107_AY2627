# Pure C (no ESP-IDF APIs) so it also runs in the linux-target unit tests.
idf_component_register(SRCS "src/thermal_features.c"
                       INCLUDE_DIRS "include"
                       REQUIRES rb_types)
