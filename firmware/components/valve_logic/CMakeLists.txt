# Valve node decision logic: pure C with timestamps passed in, so it also
# builds for the linux target and runs in testing/unit_tests.
idf_component_register(SRCS "src/valve_logic.c"
                       INCLUDE_DIRS "include"
                       REQUIRES rb_protocol)
