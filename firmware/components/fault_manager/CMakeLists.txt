# Central fault registry (TODO section 16). Depends only on FreeRTOS, so it
# also runs in the linux-target unit tests.
idf_component_register(SRCS "src/fault_manager.c"
                       INCLUDE_DIRS "include"
                       REQUIRES freertos)
