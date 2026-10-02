#pragma once

/*
 * Tunable C4002 detection parameters. One struct is used everywhere: the
 * presence node's driver and saved settings, the ESP-NOW config messages and
 * the S3's MQTT command parser. Limits are checked by rb_c4002_params_check()
 * in rb_protocol. docs/c4002_tuning.md describes each field.
 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bump when rb_c4002_params_t changes, so settings saved by older firmware are ignored. */
#define RB_C4002_PARAMS_VERSION 1u

#define RB_C4002_MAX_GATES 25u        /* 20 cm resolution; 80 cm uses the first 15 */
#define RB_C4002_GATES_80CM 15u
#define RB_C4002_ALL_GATES 0x01FFFFFFu /* bit i = gate i */

typedef enum {
    RB_C4002_RES_80CM = 0,            /* 15 gates, up to ~11 m */
    RB_C4002_RES_20CM = 1,            /* 25 gates, up to ~4.9 m */
} rb_c4002_resolution_t;

/* Threshold group. CUSTOM uses the per-gate thresholds below instead. */
typedef enum {
    RB_C4002_SENS_LOW = 0,
    RB_C4002_SENS_MID = 1,
    RB_C4002_SENS_HIGH = 2,
    RB_C4002_SENS_CUSTOM = 3,
} rb_c4002_sensitivity_t;

/* thresholds_known bits: the matching threshold array holds real values. */
enum {
    RB_C4002_THRESH_MOTION = 1u << 0,
    RB_C4002_THRESH_PRESENCE = 1u << 1,
};

typedef struct {
    uint8_t report_period_ds;         /* 1..255, 0.1 s units */
    uint16_t range_min_cm;            /* 0..1100 */
    uint16_t range_max_cm;            /* range_min_cm..1100 */
    uint8_t resolution;               /* rb_c4002_resolution_t */
    uint8_t motion_sensitivity;       /* rb_c4002_sensitivity_t */
    uint8_t presence_sensitivity;     /* rb_c4002_sensitivity_t */
    uint16_t disappear_delay_s;       /* target hold time after it disappears */
    uint8_t lock_time_ds;             /* 2..100: detection paused after occupied -> empty */
    uint32_t motion_gate_mask;        /* enabled distance gates, bit i = gate i */
    uint32_t presence_gate_mask;
    uint8_t thresholds_known;         /* RB_C4002_THRESH_* */
    uint8_t motion_thresholds[RB_C4002_MAX_GATES];   /* 0..99 per gate, for SENS_CUSTOM */
    uint8_t presence_thresholds[RB_C4002_MAX_GATES];
} rb_c4002_params_t;

/* Number of distance gates for a resolution (15 or 25). */
static inline unsigned rb_c4002_gate_count(uint8_t resolution)
{
    return resolution == RB_C4002_RES_20CM ? RB_C4002_MAX_GATES : RB_C4002_GATES_80CM;
}

#ifdef __cplusplus
}
#endif
