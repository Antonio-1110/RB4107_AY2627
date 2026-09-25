set(srcs "src/c4002_proto.c")
set(priv_requires "")
# The protocol codec is plain C so it also builds for the linux target
# (unit tests); the UART driver only exists on real chips.
idf_build_get_property(target IDF_TARGET)
if(NOT target STREQUAL "linux")
    list(APPEND srcs "src/c4002.c")
    list(APPEND priv_requires esp_driver_uart esp_driver_gpio esp_timer)
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       REQUIRES rb_types
                       PRIV_REQUIRES ${priv_requires})
