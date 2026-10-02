# Controller side of the heat map (display only): reassembles THERMAL_FRAME
# pieces from the ESP-NOW receive callback for the telemetry task to publish.
idf_component_register(SRCS "src/rb_heatmap.c"
                       INCLUDE_DIRS "include"
                       REQUIRES thermal_frame esp_common
                       PRIV_REQUIRES rb_espnow freertos esp_timer)
