# Controller only: copies the controller's own log lines into a small buffer
# for the telemetry task to publish on <prefix>/controller/log (docs/logging.md).
# The line parser is plain C, so the unit tests (linux target) build only that.
set(srcs "src/rb_log_line.c")
if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND srcs "src/rb_log_capture.c")
endif()
idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES rb_config freertos log)
