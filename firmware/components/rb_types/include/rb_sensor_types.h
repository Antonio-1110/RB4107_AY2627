#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Three-valued input. Missing sensor data is UNKNOWN, never FALSE
 * (engineering rule 5: missing data is not a safe reading).
 */
typedef enum {
    RB_UNKNOWN = 0,
    RB_FALSE = 1,
    RB_TRUE = 2,
} rb_tristate_t;

/* C4002 presence reading. Every field is reported by the sensor. */
typedef struct {
    bool valid;              /* false: no fresh, well-formed report from the sensor */
    bool presence_detected;  /* moving or stationary target present */
    bool moving_target;
    bool stationary_target;
    float distance_m;        /* distance of the reported target, NAN if none */
    uint32_t timestamp_ms;   /* node monotonic time of the reading */
} presence_reading_t;

/* Features derived from one MLX90640 32x24 frame (TODO section 3.1). */
typedef struct {
    bool valid;
    float max_temp_c;
    float min_temp_c;
    float mean_temp_c;
    float hot_region_temp_c;     /* mean of the region around the hottest pixel */
    float temp_rate_c_per_min;   /* rate of change of hot_region_temp_c */
    uint16_t pixels_above_threshold;
    uint32_t timestamp_ms;
} thermal_reading_t;

#ifdef __cplusplus
}
#endif
