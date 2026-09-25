# JSON serialisation of RB4107 telemetry and events (TODO section 21).
# Pure C: unit-tested on the linux target and validated against docs/schema.
idf_component_register(SRCS "src/rb_json_writer.c" "src/rb_json.c"
                       INCLUDE_DIRS "include")
