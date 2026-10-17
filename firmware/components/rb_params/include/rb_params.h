#pragma once

/*
 * The controller's other safety settings, which the dashboard can change (the "params" command).
 * The settings themselves, their ranges and where each one goes are in rb_params.def.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "safety.h"
#include "sensor_node.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
#define RB_PARAM(key, kind, min, max, source, options, label, get, set) RB_PARAM_##key,
#include "rb_params.def"
#undef RB_PARAM
    RB_PARAM_COUNT,
} rb_param_id_t;

typedef struct {
    const char *key;
    uint32_t min;
    uint32_t max;
} rb_param_info_t;

extern const rb_param_info_t RB_PARAMS[RB_PARAM_COUNT];

/* One value per setting, in the units of rb_params.def. */
typedef struct {
    uint32_t v[RB_PARAM_COUNT];
} rb_params_t;

/* Everything the settings change. */
typedef struct {
    safety_config_t safety;
    sensor_node_health_config_t health;
    uint32_t valve_keepalive_ms;
    uint32_t verify_period_ms;
} rb_params_target_t;

/* The settings' index by key, or -1. */
int rb_params_find(const char *key);

/* The values in force. */
void rb_params_read(const rb_params_target_t *t, rb_params_t *out);

/* Every value within its range? */
bool rb_params_in_range(const rb_params_t *p);

/*
 * Apply in-range values. The caller still checks the result (safety_config_valid() and
 * rb_params_target_valid()): some settings only make sense together.
 */
void rb_params_write(const rb_params_t *p, rb_params_target_t *t);

/* Changes when the table's keys or their order change: a saved copy from another firmware is not used. */
uint32_t rb_params_layout_hash(void);

/* The settings that depend on each other: a node goes STALE before it goes OFFLINE. */
bool rb_params_target_valid(const rb_params_target_t *t);

#ifdef __cplusplus
}
#endif
