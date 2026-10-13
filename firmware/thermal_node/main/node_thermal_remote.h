#pragma once

/*
 * Hot-pixel threshold = the dashboard's cooking temperature (docs/remote_reset.md):
 *
 *   dashboard -> Django -> MQTT controller/command "cooking_temp"
 *     -> controller -> ESP-NOW THERMAL_CONFIG -> this node
 *
 * A new threshold is used from the next frame and saved in NVS, so it
 * survives a reboot; "defaults" erases it and goes back to
 * RB_THERMAL_HOT_PIXEL_THRESHOLD_DC. The node sends no answer: the threshold
 * in use rides in every heat-map picture (hot_threshold_c), which is where
 * the dashboard reads it. The safety logic never uses this threshold.
 */
#include "esp_err.h"

/* Use the saved threshold, if any. Call before node_thermal_start(). */
void node_thermal_remote_load(void);

/* Listen for THERMAL_CONFIG. Call after rb_node_link_start(). */
esp_err_t node_thermal_remote_start(void);
