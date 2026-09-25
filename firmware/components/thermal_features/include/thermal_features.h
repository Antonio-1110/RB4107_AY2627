#pragma once

/*
 * Feature extraction for one 32x24 thermal frame (TODO section 3.1).
 *
 * Every threshold is a parameter. The defaults in menuconfig are placeholders
 * until they have been checked against real measurements.
 */
#include <stdbool.h>
#include <stdint.h>
#include "rb_sensor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define THERMAL_COLS 32
#define THERMAL_ROWS 24
#define THERMAL_PIXELS (THERMAL_COLS * THERMAL_ROWS)
#define THERMAL_RATE_SAMPLES 16

typedef struct {
    float hot_pixel_threshold_c;   /* counted in pixels_above_threshold */
    uint8_t hot_region_radius;     /* 1 -> 3x3 region around the hottest pixel */
    uint32_t rate_window_ms;       /* span used for temp_rate_c_per_min */
    float valid_min_c;             /* pixels outside [min, max] count as invalid */
    float valid_max_c;
    uint16_t max_invalid_pixels;   /* above this the frame is invalid */
} thermal_features_config_t;

/* History of hot-region temperatures for the rate-of-change estimate. */
typedef struct {
    uint32_t t_ms[THERMAL_RATE_SAMPLES];
    float temp_c[THERMAL_RATE_SAMPLES];
    uint8_t head;
    uint8_t count;
} thermal_rate_tracker_t;

/* Extra per-frame details for diagnostics. */
typedef struct {
    uint8_t hottest_col;
    uint8_t hottest_row;
    uint16_t invalid_pixels;
} thermal_frame_info_t;

void thermal_rate_reset(thermal_rate_tracker_t *tracker);

/*
 * Compute features for one frame. Returns out->valid. An invalid frame is not
 * added to the rate history. temp_rate_c_per_min is NAN until the history
 * covers at least a quarter of rate_window_ms. info may be NULL.
 */
bool thermal_features_compute(const float frame[THERMAL_PIXELS], uint32_t now_ms,
                              const thermal_features_config_t *config, thermal_rate_tracker_t *tracker,
                              thermal_reading_t *out, thermal_frame_info_t *info);

#ifdef __cplusplus
}
#endif
