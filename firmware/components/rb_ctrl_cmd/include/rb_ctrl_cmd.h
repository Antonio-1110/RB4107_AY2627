#pragma once

/*
 * The dashboard -> controller command, published by Django on
 * <prefix>/controller/command. JSON contract in docs/remote_reset.md and
 * docs/schema/rb4107_mqtt.schema.json ("controller_command"). Example:
 *
 *   {"schema_version":2,"type":"controller_command","controller_id":"controller_01",
 *    "request_id":7,"action":"reset"}
 *
 * Actions:
 *   "reset"                     the operator reset / acknowledge, as the
 *                               diagnostic console's `reset` command
 *   "presence_filter"           change the presence filter of the running
 *                               state machine; needs absence_ms, return_ms and
 *                               return_gap_ms (milliseconds, limits below)
 *   "presence_filter_defaults"  back to the menuconfig values
 *   "thermal_threshold"         send a new hot-pixel threshold to the thermal
 *                               node, which saves it; needs
 *                               hot_pixel_threshold_dc (0.1 degC, 0..3000)
 *   "thermal_threshold_defaults" the thermal node forgets the saved value and
 *                               goes back to its menuconfig one
 *
 * Flat object, strings and integers only, so a small hand-written parser is
 * enough.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Dashboard limits for "presence_filter" (the menuconfig ranges are wider). */
#define RB_CTRL_ABSENCE_MAX_MS 10000
#define RB_CTRL_RETURN_MAX_MS 10000
#define RB_CTRL_RETURN_GAP_MAX_MS 5000
/* Dashboard limit for "thermal_threshold": 0..300.0 degC. */
#define RB_CTRL_HOT_PIXEL_MAX_DC 3000

typedef enum {
    RB_CTRL_ACTION_RESET = 1,
    RB_CTRL_ACTION_PRESENCE_FILTER,
    RB_CTRL_ACTION_PRESENCE_FILTER_DEFAULTS,
    RB_CTRL_ACTION_THERMAL_THRESHOLD,
    RB_CTRL_ACTION_THERMAL_THRESHOLD_DEFAULTS,
} rb_ctrl_action_t;

typedef struct {
    rb_ctrl_action_t action;
    uint16_t request_id;
    char controller_id[40];
    /* RB_CTRL_ACTION_PRESENCE_FILTER only. */
    uint32_t absence_ms;
    uint32_t return_ms;
    uint32_t return_gap_ms;
    /* RB_CTRL_ACTION_THERMAL_THRESHOLD only. */
    uint16_t hot_pixel_threshold_dc;
} rb_ctrl_cmd_t;

/*
 * Parse and check one command. The header keys and action are required; the
 * filter values are required with "presence_filter" and refused otherwise,
 * and the same goes for hot_pixel_threshold_dc with "thermal_threshold".
 * No other key is allowed. On failure returns false and writes a short
 * reason into err.
 */
bool rb_ctrl_cmd_parse(const char *json, size_t len, rb_ctrl_cmd_t *out, char *err, size_t err_len);

#ifdef __cplusplus
}
#endif
