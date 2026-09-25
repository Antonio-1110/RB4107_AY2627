# Shared C6 <-> S3 wire protocol. Pure C: builds for every target, including linux.
idf_component_register(SRCS "src/rb_protocol.c"
                       INCLUDE_DIRS "include"
                       REQUIRES rb_types)
