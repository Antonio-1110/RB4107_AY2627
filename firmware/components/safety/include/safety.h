#pragma once

/*
 * RB4107 safety state machine (TODO sections 9-11).
 *
 *   BOOT -> SELF_TEST -> IDLE -> MONITORING -> UNATTENDED -> WARNING -> SHUTDOWN
 *                                   (any state after SELF_TEST) -> FAULT
 *
 * - Hardware independent: safety_step() takes inputs plus a monotonic
 *   timestamp and returns the outputs (buzzer pattern, shutdown request).
 *   The caller drives the actual buzzer and relay.
 * - Non-blocking: every timer is "now - start >= timeout", checked on each
 *   step. Nothing in here waits.
 * - Never depends on MQTT or the network.
 * - UNKNOWN presence or invalid thermal data never counts as safe: it leads
 *   to FAULT, and FAULT never delays a shutdown that was already due.
 * - Every transition goes through one function that calls the transition
 *   callback, which is where logging and events hook in.
 */
#include <stdbool.h>
#include <stdint.h>
#include "rb_sensor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SAFETY_BOOT = 0,
    SAFETY_SELF_TEST,
    SAFETY_IDLE,
    SAFETY_MONITORING,
    SAFETY_UNATTENDED,
    SAFETY_WARNING,
    SAFETY_SHUTDOWN,
    SAFETY_FAULT,
    SAFETY_STATE_COUNT,
} safety_state_t;

typedef enum {
    SAFETY_SELFTEST_PENDING = 0,
    SAFETY_SELFTEST_PASS,
    SAFETY_SELFTEST_FAIL,
} safety_selftest_t;

/* OPEN QUESTION: does "90 s" mean total unattended time or 90 s after the warning? */
typedef enum {
    SAFETY_SHUTDOWN_AFTER_UNATTENDED_START = 0, /* shutdown_timeout counts from the start of UNATTENDED */
    SAFETY_SHUTDOWN_AFTER_WARNING_START,        /* shutdown_timeout counts from entering WARNING */
} safety_shutdown_timing_t;

/* OPEN QUESTION: what happens if the person returns during WARNING? */
typedef enum {
    SAFETY_WARNING_EXIT_ON_PRESENCE = 0,  /* person back -> MONITORING */
    SAFETY_WARNING_EXIT_ON_ACK,           /* person must also acknowledge (reset input) */
} safety_warning_exit_t;

/* OPEN QUESTION: what happens if the temperature falls while unattended? */
typedef enum {
    SAFETY_COOLING_KEEP_TIMERS = 0,       /* keep counting to warning/shutdown */
    SAFETY_COOLING_RETURN_TO_IDLE,        /* heat gone -> IDLE, timers cancelled */
} safety_cooling_policy_t;

/* Buzzer requests. The buzzer driver maps them to patterns. */
typedef enum {
    SAFETY_BUZZER_OFF = 0,
    SAFETY_BUZZER_WARNING,
    SAFETY_BUZZER_SHUTDOWN,
    SAFETY_BUZZER_FAULT,
} safety_buzzer_t;

typedef struct {
    safety_buzzer_t buzzer;
    bool shutdown;            /* true: cut power to the appliance */
} safety_outputs_t;

typedef struct safety_inputs safety_inputs_t;

/*
 * Optional hook so the temperature trend can change the timing later
 * (TODO section 10, "temperature interaction"). It may lower the two
 * timeouts it is given. NULL = fixed timing.
 */
typedef void (*safety_timing_hook_t)(const safety_inputs_t *inputs, uint32_t *warning_timeout_ms,
                                     uint32_t *shutdown_timeout_ms, void *ctx);

typedef struct {
    uint32_t self_test_timeout_ms;          /* SELF_TEST -> FAULT if no result in time */

    /* Heat detection (UNVERIFIED placeholders until thermal data is collected). */
    float heat_on_temp_c;                   /* hot-region temp that means cooking has started */
    float heat_off_temp_c;                  /* hysteresis: cooking considered over below this */
    float heat_on_rate_c_per_min;           /* also "cooking" when rising this fast (above heat_off); 0 = off */

    /* Presence filtering. */
    uint32_t absence_debounce_ms;           /* continuous absence needed before UNATTENDED */
    uint32_t presence_return_debounce_ms;   /* continuous presence needed to cancel UNATTENDED/WARNING */

    /* Timers. */
    uint32_t warning_timeout_ms;
    uint32_t shutdown_timeout_ms;
    safety_shutdown_timing_t shutdown_timing;

    /* Policies for the open questions. */
    safety_warning_exit_t warning_exit;
    safety_cooling_policy_t cooling_policy;
    uint32_t fault_shutdown_timeout_ms;     /* time in FAULT before SHUTDOWN; 0 = never */

    safety_timing_hook_t timing_hook;
    void *timing_hook_ctx;
} safety_config_t;

struct safety_inputs {
    rb_tristate_t presence;                 /* UNKNOWN when the data is missing or invalid */
    bool thermal_valid;
    float hot_region_temp_c;
    float temp_rate_c_per_min;
    safety_selftest_t self_test;
    bool reset_request;                     /* operator reset / acknowledge (e.g. button) */
    bool safety_fault;                      /* any other safety-relevant fault (fault manager) */
};

typedef void (*safety_transition_cb_t)(safety_state_t from, safety_state_t to, const char *reason, uint32_t now_ms,
                                       void *ctx);

typedef struct {
    safety_config_t cfg;
    safety_state_t state;
    uint32_t state_entered_ms;
    safety_outputs_t outputs;

    bool heat_active;                 /* heat detector state (with hysteresis) */

    bool absent_timing;               /* absence debounce running */
    uint32_t absent_since_ms;
    bool present_timing;              /* presence-return debounce running */
    uint32_t present_since_ms;

    uint32_t monitoring_since_ms;     /* last entry into MONITORING */
    bool unattended_active;           /* unattended timeline in progress (kept through FAULT) */
    uint32_t unattended_start_ms;
    bool warning_started;
    uint32_t warning_start_ms;

    uint32_t transitions;
    const char *last_reason;

    safety_transition_cb_t on_transition;
    void *cb_ctx;
} safety_sm_t;

/* Returns false (and fixes nothing) if the configuration is inconsistent. */
bool safety_config_valid(const safety_config_t *cfg);

void safety_init(safety_sm_t *sm, const safety_config_t *cfg, safety_transition_cb_t cb, void *cb_ctx, uint32_t now_ms);

/*
 * Swap the configuration at runtime (e.g. accelerated test timers). Running
 * timers keep their start times and are checked against the new timeouts.
 */
bool safety_set_config(safety_sm_t *sm, const safety_config_t *cfg);

/* Advance the machine. Call it periodically and on every new input. */
const safety_outputs_t *safety_step(safety_sm_t *sm, const safety_inputs_t *in, uint32_t now_ms);

/* Time spent unattended so far (0 if not on the unattended timeline). */
uint32_t safety_unattended_ms(const safety_sm_t *sm, uint32_t now_ms);

const char *safety_state_name(safety_state_t state);
const char *safety_buzzer_name(safety_buzzer_t buzzer);

#ifdef __cplusplus
}
#endif
