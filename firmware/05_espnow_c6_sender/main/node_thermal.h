#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "rb_sensor_types.h"

/* Start the MLX90640 acquisition task (retries sensor init in the background). */
esp_err_t node_thermal_start(void);

/*
 * Latest thermal features. Invalid if the sensor never came up, the last good
 * frame is older than CONFIG_RB_MLX_STALE_TIMEOUT_MS, or the frame failed
 * validation. *no_data tells "no frames" apart from "bad frames".
 */
void node_thermal_get(thermal_reading_t *out, bool *no_data);
