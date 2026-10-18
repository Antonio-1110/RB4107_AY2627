#pragma once

/*
 * Controller runtime (TODO section 12).
 *
 *   ESP-NOW callback -> rx queue -> safety task -> state machine -> outputs hook
 *                                        |
 *                                        +-> event queue -> telemetry task (MQTT, lower priority)
 *                                        +-> snapshot (mutex) -> diagnostics / telemetry
 *
 * The safety task owns the sensor-node states and the state machine; nothing
 * else touches them. It never blocks on anything the telemetry side owns:
 * events are posted with a zero timeout and the snapshot lock is only held
 * for a struct copy. An MQTT stall therefore can't stop safety processing.
 */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "rb_espnow.h"
#include "rb_params.h"
#include "rb_protocol.h"
#include "safety.h"
#include "sensor_node.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RB_EVT_STATE_CHANGE = 0,  /* safety transition */
    RB_EVT_NODE,              /* sensor-node health events (node_event_t bits) */
    RB_EVT_FAULT,             /* fault raised/cleared (fault manager, section 16) */
    RB_EVT_SENSOR_STATE,      /* presence / cooking / sensor flags / debounce timers changed: publish now */
} rb_event_type_t;

typedef struct {
    rb_event_type_t type;
    uint32_t mono_ms;         /* monotonic time of the event */
    union {
        struct {
            safety_state_t from;
            safety_state_t to;
            const char *reason;   /* string literal owned by the safety component */
        } state;
        struct {
            node_slot_t slot;
            uint32_t node_id;
            uint32_t events;
        } node;
        struct {
            uint16_t id;
            bool active;
        } fault;
    };
} rb_event_t;

typedef struct {
    uint32_t mono_ms;
    safety_state_t state;
    uint32_t state_duration_ms;
    uint32_t unattended_ms;
    safety_outputs_t outputs;
    bool reset_required;          /* latched SHUTDOWN: waiting for an operator reset */
    uint32_t warning_timeout_ms;  /* the safety config in use (normal or test timers) */
    uint32_t shutdown_timeout_ms;
    safety_shutdown_timing_t shutdown_timing;
    const char *last_reason;
    uint32_t transitions;
    node_set_t nodes;             /* every sensor node's state */
    node_inputs_t inputs;         /* combined inputs; presence is what the state machine used */
    uint32_t loop_count;          /* safety loop iterations: proves the task is alive */
    uint32_t last_loop_ms;
    uint32_t events_dropped;
    bool test_timers;             /* accelerated diagnostic timers active */
    uint32_t absence_debounce_ms; /* presence filter in use */
    uint32_t presence_return_debounce_ms;
    uint32_t presence_return_gap_ms;
    bool presence_filter_remote;  /* set from the dashboard, not menuconfig */
    float heat_on_temp_c;         /* cooking from this hottest-pixel temperature */
    float heat_off_temp_c;        /* cooking over below this one */
    bool cooking_temp_remote;     /* cooking temperature set from the dashboard, not menuconfig */
    bool cooking_active;          /* the heat detector's state: cooking, per the thermal camera */
    safety_heat_phase_t heat_phase;   /* heating / holding / cooling, from the temperature trend */
    float trend_rate_c_per_min;   /* NAN until there is enough data */
    float settle_c;               /* where the temperature is heading, NAN if unknown */
    safety_hold_t hold;           /* what holds a due buzzer or shutdown back right now */
    safety_tier_t tier;           /* temperature tier in force */
    uint32_t eff_warning_ms;      /* the buzzer and shutdown times in force (base, tier and hook applied) */
    uint32_t eff_shutdown_ms;
    safety_config_t tiers;        /* only the tier fields are read: tier1_*, tier2_* */
    bool tiers_remote;            /* tiers set from the dashboard, not menuconfig */
    bool paused;                  /* the clock is stopped: someone is at the stove (pause budget) */
    uint32_t pause_used_ms;
    uint32_t pause_budget_ms;     /* in force on this timeline, ms */
    uint32_t pause_budget_cfg_ms; /* the configured budget */
    uint8_t pause_budget_pct;
    bool fusion_all;              /* someone is at the stove when every working radar sees them */
    bool attendance_remote;       /* fusion and pause budget set from the dashboard */
    bool radar_rules_remote;      /* radar state/distance rules set from the dashboard */
    safety_cookware_t cookware;   /* cookware profile in use */
    float cookware_tau_s;
    bool cookware_remote;         /* profile set from the dashboard, not menuconfig */
    bool presence_unknown;        /* flag: presence data missing/invalid (safety.h) */
    bool thermal_lost;            /* flag: thermal data missing/invalid */
    uint32_t absent_ms;           /* absence debounce progress, 0 = not running */
    uint32_t returning_ms;        /* return debounce progress, 0 = not running */
    rb_params_t params;           /* the other safety settings in force (rb_params.def) */
    bool params_remote;           /* set from the dashboard, not menuconfig */
} rb_snapshot_t;

/* Hooks the application provides. All are optional and all run in the safety task. */
typedef struct {
    /* Drive the buzzer and relay. Must not block. */
    void (*apply_outputs)(const safety_outputs_t *outputs, void *ctx);
    /* Called at the start of every safety-loop iteration (fault polling etc.). Must not block. */
    void (*tick)(uint32_t now_ms, void *ctx);
    /* Polled while in SELF_TEST. NULL = pass straight away. */
    safety_selftest_t (*self_test)(void *ctx);
    /* Any safety-relevant fault active (fault manager)? */
    bool (*safety_fault_active)(void *ctx);
    /* Told about one node's health events (fault manager raises/clears faults here). */
    void (*node_events)(const sensor_node_state_t *node, node_slot_t slot, uint32_t events, void *ctx);
    /* Last chance to change the inputs (diagnostic simulation, section 27). */
    void (*override_inputs)(safety_inputs_t *inputs, void *ctx);
    /* Every packet accepted from a configured node (remote C4002 tuning learns MACs and ACKs here). */
    void (*node_packet)(const rb_espnow_rx_t *rx, void *ctx);
    /* A packet from a valve node (wireless gas valve). Return false if it isn't one we expect. */
    bool (*valve_packet)(const rb_packet_t *packet, uint32_t rx_ms, void *ctx);
    /* The settings rb_params.def keeps outside the safety task changed: the valve keep-alive and the
     * shutdown read-back period. */
    void (*params_applied)(const rb_params_target_t *target, void *ctx);
    void *ctx;
} rb_controller_hooks_t;

typedef struct {
    safety_config_t safety;
    sensor_node_health_config_t health;
    node_set_config_t nodes;
    uint32_t valve_keepalive_ms;  /* only reported and passed to params_applied: the application uses them */
    uint32_t verify_period_ms;
    uint32_t rx_queue_len;
    uint32_t event_queue_len;
    uint32_t tick_ms;
    uint32_t safety_task_priority;
} rb_controller_config_t;

rb_controller_config_t rb_controller_config_from_kconfig(void);

/*
 * Create the queues. Call it before anything that posts events (e.g. the
 * fault manager at boot) so no early event is lost. rb_controller_start()
 * calls it if needed.
 */
esp_err_t rb_controller_init(const rb_controller_config_t *config);

esp_err_t rb_controller_start(const rb_controller_config_t *config, const rb_controller_hooks_t *hooks);

/* Queue the ESP-NOW receiver posts to (item type rb_espnow_rx_t). */
QueueHandle_t rb_controller_rx_queue(void);

/* Inject a packet as if it came over ESP-NOW (simulation). Non-blocking. */
bool rb_controller_inject(const rb_packet_t *packet);

/* Post an event from another task (e.g. the fault manager). Non-blocking. */
bool rb_controller_post_event(const rb_event_t *event);

/* Next event for the telemetry side. */
bool rb_controller_next_event(rb_event_t *out, TickType_t wait);

/*
 * Copy of the latest safety-task state. Returns false (and an all-zero *out)
 * if the snapshot could not be read in time; callers must not publish it then.
 */
bool rb_controller_get_snapshot(rb_snapshot_t *out);

/* Number of events dropped because the event queue was full. */
uint32_t rb_controller_events_dropped(void);

/*
 * Packets dropped because their node ID isn't configured or the node has the
 * wrong role (a misconfigured node). Only valid from the safety task's hooks.
 */
uint32_t rb_controller_foreign_packets(void);

/* Operator reset / acknowledge (button, console). Consumed by the safety task. */
void rb_controller_request_reset(void);

/*
 * Swap the safety configuration (e.g. accelerated test timers). Applied by
 * the safety task at its next step. The logic is the same; only the numbers
 * change.
 */
esp_err_t rb_controller_set_safety_config(const safety_config_t *config, bool test_timers);

/* The presence filter and buzzer/shutdown time fields of safety_config_t. */
typedef struct {
    uint32_t absence_debounce_ms;
    uint32_t presence_return_debounce_ms;
    uint32_t presence_return_gap_ms;
    uint32_t warning_timeout_ms;
    uint32_t shutdown_timeout_ms;
} rb_presence_filter_t;

/*
 * Override the presence filter and the warning/shutdown times (dashboard
 * tuning). It stays on top of any later rb_controller_set_safety_config()
 * (the accelerated test timers included) until a reboot or a call with NULL,
 * which goes back to the configured values. Applied at the next step; values
 * safety_config_valid() refuses (shutdown not after the warning) are logged
 * and ignored.
 */
esp_err_t rb_controller_set_presence_filter(const rb_presence_filter_t *filter);

/* Dashboard limits for the cooking temperature, as RB_SAFETY_HEAT_ON_DC. */
#define RB_COOKING_TEMP_MIN_C 0.0f
#define RB_COOKING_TEMP_MAX_C 300.0f

/*
 * Override the cooking temperature (heat_on_temp_c; heat_off_temp_c follows
 * it with the configured gap). Stays on top of any later
 * rb_controller_set_safety_config() until a reboot or a call with NAN, which
 * goes back to the configured values. Applied at the next step. Saving it
 * across reboots is the caller's job.
 */
esp_err_t rb_controller_set_cooking_temp(float on_c);

/*
 * Pick the cookware profile (mixed, light, heavy, liquid), or SAFETY_COOKWARE_COUNT for the
 * menuconfig one. Stays on top of any later rb_controller_set_safety_config() until a reboot.
 * Applied at the next step. Saving it across reboots is the caller's job.
 */
esp_err_t rb_controller_set_cookware(safety_cookware_t profile);

/* Who counts as at the stove, and how long a pass may stop the clock. */
typedef struct {
    uint32_t pause_budget_ms;
    uint8_t pause_budget_pct;
    bool fusion_all;
} rb_attendance_t;

/*
 * Replace the fusion and the pause budget, or NULL for the menuconfig ones. Stays on top of any later
 * rb_controller_set_safety_config() until a reboot or a call with NULL. Applied at the next step. Saving it
 * across reboots is the caller's job.
 */
esp_err_t rb_controller_set_attendance(const rb_attendance_t *attendance);

/* The radar state and distance rules (sensor_node.h), as the dashboard sets them. */
typedef struct {
    radar_rules_mode_t mode;
    radar_rules_config_t rules;
} rb_radar_rules_t;

/*
 * Replace the radar rules, or NULL for the menuconfig ones. Applied at the next step. Saving it across
 * reboots is the caller's job.
 */
esp_err_t rb_controller_set_radar_rules(const rb_radar_rules_t *rules);

/* The temperature tiers of safety_config_t, as the dashboard sets them. */
typedef struct {
    float tier1_temp_c;
    uint32_t tier1_warning_ms;
    uint32_t tier1_shutdown_ms;
    float tier2_temp_c;
    float tier2_rate_c_per_min;
    float tier2_rate_min_temp_c;
    uint32_t tier2_warning_ms;
    uint32_t tier2_shutdown_ms;
} rb_tiers_t;

/*
 * Replace the temperature tiers, or NULL for the menuconfig ones. Stays on top of any later
 * rb_controller_set_safety_config() until a reboot or a call with NULL. Applied at the next step; values
 * safety_config_valid() refuses (a shutdown not after its warning, CRITICAL below ELEVATED) are logged and
 * ignored. Saving it across reboots is the caller's job.
 */
esp_err_t rb_controller_set_tiers(const rb_tiers_t *tiers);

/*
 * Replace the other safety settings (rb_params.def), or NULL for the menuconfig and built-in ones. Stays on
 * top of any later rb_controller_set_safety_config() until a reboot or a call with NULL. Applied at the next
 * step; values out of range, or that safety_config_valid() / rb_params_target_valid() refuse, are logged and
 * ignored. Saving it across reboots is the caller's job.
 */
esp_err_t rb_controller_set_params(const rb_params_t *params);

#ifdef __cplusplus
}
#endif
