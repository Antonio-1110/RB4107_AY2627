# Shared C6 <-> S3 wire protocol. Pure C: builds for every target, including linux.
idf_component_register(SRCS "src/rb_protocol.c"
                       INCLUDE_DIRS "include"
                       REQUIRES rb_types)

# roundf() lives in libm, which the linux (host test) target doesn't link by default.
idf_build_get_property(target IDF_TARGET)
if(target STREQUAL "linux")
    target_link_libraries(${COMPONENT_LIB} PUBLIC m)
endif()
