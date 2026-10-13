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
 *   "presence_filter"           change the presence filter and the
 *                               buzzer/shutdown times of the running state
 *                               machine; needs absence_ms, return_ms,
 *                               return_gap_ms, warning_ms and shutdown_ms
 *                               (milliseconds, limits below)
 *   "presence_filter_defaults"  back to the menuconfig values
 *   "cooking_temp"              set the cooking temperature (hottest pixel
 *                               at or above it = cooking), saved on the
 *                               controller; needs cooking_temp_dc (0.1 degC,
 *                               0..3000)
 *   "cooking_temp_defaults"     forget the saved value: back to menuconfig
 *   "cookware"                  pick the cookware profile, saved on the
 *                               controller; needs cookware: "mixed", "light",
 *                               "heavy" or "liquid"
 *   "cookware_defaults"         forget the saved profile: back to menuconfig
 *   "tiers"                     set the temperature tiers (docs/safety_state_machine.md),
 *                               saved on the controller; needs all eight values:
 *                               tier1_temp_dc, tier1_warning_ms, tier1_shutdown_ms,
 *                               tier2_temp_dc, tier2_rate_dc_per_min,
 *                               tier2_rate_min_temp_dc, tier2_warning_ms,
 *                               tier2_shutdown_ms (0.1 degC, 0.1 degC/min, ms)
 *   "tiers_defaults"            forget the saved tiers: back to menuconfig
 *   "attendance"                set who counts as at the stove and the pause budget, saved
 *                               on the controller; needs pause_budget_ms (0..600000),
 *                               pause_budget_pct (0..100) and presence_fusion ("all" every
 *                               working radar sees them, "any" either does)
 *   "attendance_defaults"       forget the saved values: back to menuconfig
 *   "radar_rules"               set the radar state and distance rules, saved on the controller;
 *                               needs radar_mode ("off", "observe", "enforce"),
 *                               radar_quiet_gap_ms, radar_dwell_ms, radar_stationary_min_ms,
 *                               radar_settle_window_ms, radar_settle_span_cm, radar_zone_max_cm
 *   "radar_rules_defaults"      forget the saved rules: back to menuconfig
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
#define RB_CTRL_TIMEOUT_MIN_MS 1000 /* warning_ms and shutdown_ms, as the menuconfig 1..3600 s */
#define RB_CTRL_TIMEOUT_MAX_MS 3600000
/* Dashboard limit for "cooking_temp": 0..300.0 degC, as RB_SAFETY_HEAT_ON_DC. */
#define RB_CTRL_COOKING_TEMP_MAX_DC 3000
/* Dashboard limit for the tier rate: 0..1000.0 degC/min. */
#define RB_CTRL_TIER_RATE_MAX_DC_PER_MIN 10000

typedef enum {
    RB_CTRL_ACTION_RESET = 1,
    RB_CTRL_ACTION_PRESENCE_FILTER,
    RB_CTRL_ACTION_PRESENCE_FILTER_DEFAULTS,
    RB_CTRL_ACTION_COOKING_TEMP,
    RB_CTRL_ACTION_COOKING_TEMP_DEFAULTS,
    RB_CTRL_ACTION_COOKWARE,
    RB_CTRL_ACTION_COOKWARE_DEFAULTS,
    RB_CTRL_ACTION_TIERS,
    RB_CTRL_ACTION_TIERS_DEFAULTS,
    RB_CTRL_ACTION_ATTENDANCE,
    RB_CTRL_ACTION_ATTENDANCE_DEFAULTS,
    RB_CTRL_ACTION_RADAR_RULES,
    RB_CTRL_ACTION_RADAR_RULES_DEFAULTS,
} rb_ctrl_action_t;

typedef struct {
    rb_ctrl_action_t action;
    uint16_t request_id;
    char controller_id[40];
    /* RB_CTRL_ACTION_PRESENCE_FILTER only. */
    uint32_t absence_ms;
    uint32_t return_ms;
    uint32_t return_gap_ms;
    uint32_t warning_ms;
    uint32_t shutdown_ms;
    /* RB_CTRL_ACTION_COOKING_TEMP only. */
    uint16_t cooking_temp_dc;
    /* RB_CTRL_ACTION_COOKWARE only: "mixed", "light", "heavy" or "liquid". */
    char cookware[12];
    /* RB_CTRL_ACTION_TIERS only. Temperatures in 0.1 degC (0 = that tier off), rate in 0.1 degC/min. */
    uint16_t tier1_temp_dc;
    uint32_t tier1_warning_ms;
    uint32_t tier1_shutdown_ms;
    uint16_t tier2_temp_dc;
    uint16_t tier2_rate_dc_per_min;
    uint16_t tier2_rate_min_temp_dc;
    uint32_t tier2_warning_ms;
    uint32_t tier2_shutdown_ms;
    /* RB_CTRL_ACTION_ATTENDANCE only. */
    uint32_t pause_budget_ms;
    uint8_t pause_budget_pct;
    bool fusion_all;
    /* RB_CTRL_ACTION_RADAR_RULES only: radar_mode "off" / "observe" / "enforce", times in ms, distances in cm. */
    char radar_mode[10];
    uint32_t radar_quiet_gap_ms;
    uint32_t radar_dwell_ms;
    uint32_t radar_stationary_min_ms;
    uint32_t radar_settle_window_ms;
    uint16_t radar_settle_span_cm;
    uint16_t radar_zone_max_cm;
} rb_ctrl_cmd_t;

/*
 * Parse and check one command. The header keys and action are required; the
 * filter values are required with "presence_filter" and refused otherwise,
 * and the same goes for cooking_temp_dc with "cooking_temp".
 * No other key is allowed. On failure returns false and writes a short
 * reason into err.
 */
bool rb_ctrl_cmd_parse(const char *json, size_t len, rb_ctrl_cmd_t *out, char *err, size_t err_len);

#ifdef __cplusplus
}
#endif
