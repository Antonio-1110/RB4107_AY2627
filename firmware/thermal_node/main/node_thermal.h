#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "rb_sensor_types.h"
#include "thermal_frame.h"

/* Start the MLX90640 acquisition task (retries sensor init in the background). */
esp_err_t node_thermal_start(void);

/*
 * Latest thermal features. Invalid if the sensor never came up, the last good
 * frame is older than CONFIG_RB_MLX_STALE_TIMEOUT_MS, or the frame failed
 * validation. *no_data tells "no frames" apart from "bad frames".
 */
void node_thermal_get(thermal_reading_t *out, bool *no_data);

/*
 * Hot-pixel threshold (degC) used from the next frame on: counted in
 * pixels_above_threshold and shown on the heat map. Starts at
 * RB_THERMAL_HOT_PIXEL_THRESHOLD_DC; the dashboard can change it
 * (node_thermal_remote.h).
 */
void node_thermal_set_hot_threshold(float threshold_c);
float node_thermal_hot_threshold(void);

/*
 * Latest heat-map picture, if it is newer than *number (pass the number from
 * the previous call, 0 the first time). Display only. Always false with
 * RB_THERMAL_HEATMAP off.
 */
bool node_thermal_get_heatmap(thermal_frame_t *out, uint32_t *number);
