# Remote C4002 tuning on the S3: parses the dashboard's MQTT command JSON into
# a C4002_CONFIG message and writes the c4002_config reply. Pure C:
# unit-tested on the linux target.
idf_component_register(SRCS "src/rb_c4002_cmd.c"
                       INCLUDE_DIRS "include"
                       REQUIRES rb_protocol rb_json)
