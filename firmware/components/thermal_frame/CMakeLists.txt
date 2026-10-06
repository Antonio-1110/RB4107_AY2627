# Heat-map frames (display only): 1-byte pixels on the thermal node, reassembly on the S3.
# Pure C (no ESP-IDF APIs) so it also runs in the linux-target unit tests.
idf_component_register(SRCS "src/thermal_frame.c"
                       INCLUDE_DIRS "include"
                       REQUIRES rb_protocol)

# roundf()/ceilf() live in libm, which the linux (host test) target doesn't link by default.
idf_build_get_property(target IDF_TARGET)
if(target STREQUAL "linux")
    target_link_libraries(${COMPONENT_LIB} PUBLIC m)
endif()
