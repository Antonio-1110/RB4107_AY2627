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
 * - Missing sensor data is never read as "safe", but it no longer causes
 *   FAULT. It raises a flag (presence_unknown / thermal_lost) instead:
 *     - presence UNKNOWN, nothing cooking: flag only, no timer;
 *     - presence UNKNOWN while cooking (per the thermal camera): flag, and
 *       presence counts as absent so the unattended timeline runs;
 *     - thermal camera lost: flag only. The last known cooking state is kept,
 *       so a timeline already running is not cancelled and no cooking is
 *       invented.
 *   FAULT is only for the controller itself: a failed self-test or a failed
 *   shutdown output (safety_fault). It still shuts down after
 *   fault_shutdown_timeout_ms, and never delays a shutdown already due.
 * - Presence debounce lives here and only here. A presence blip never
 *   restarts the absence timer; only a confirmed return does.
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

/*
 * Cookware profile: how fast the pans and pots at this stall lose heat. It sets
 * the time constant tau of Newton's cooling (T falls toward the room
 * temperature as e^(-t/tau)), which the cooking judgment uses to tell a pan
 * that is just cooling from one that still has heat going in. The values are
 * engineering estimates from typical mass and heat capacity, not measurements;
 * docs/safety_state_machine.md ("Cooking judgment") explains them.
 */
typedef enum {
    SAFETY_COOKWARE_MIXED = 0, /* default: anything */
    SAFETY_COOKWARE_LIGHT,     /* woks, thin aluminium or stainless pans */
    SAFETY_COOKWARE_HEAVY,     /* cast iron, thick clad pans */
    SAFETY_COOKWARE_LIQUID,    /* pots of soup, stock, boiling water */
    SAFETY_COOKWARE_COUNT,
} safety_cookware_t;

float safety_cookware_tau_s(safety_cookware_t profile);
const char *safety_cookware_name(safety_cookware_t profile);                /* "mixed", "light", ... */
bool safety_cookware_from_name(const char *name, safety_cookware_t *out);

/* What the heat is doing, from the trend of the hottest pixel (see safety_cooking_phase()). */
typedef enum {
    SAFETY_HEAT_OFF = 0,      /* below the cooking temperature */
    SAFETY_HEAT_CHECKING,     /* cooking, but not enough trend data yet: counts as heat going in */
    SAFETY_HEAT_HEATING,      /* heading to a clearly higher temperature */
    SAFETY_HEAT_HOLDING,      /* steady, or settling somewhere hot: heat is going in (low flame, simmer, boil) */
    SAFETY_HEAT_COOLING,      /* heading toward the room temperature: no heat going in */
} safety_heat_phase_t;

/* Why a due warning or shutdown is being held back (telemetry: safety.hold). */
typedef enum {
    SAFETY_HOLD_NONE = 0,
    SAFETY_HOLD_RETURN,       /* the radar sees someone and the return is being confirmed (bounded) */
    SAFETY_HOLD_COOLING,      /* the pan is cooling down: the timers wait for a plateau or for it to end */
} safety_hold_t;

/*
 * Temperature tiers: how hot the pan is decides how long the person may be away. The
 * timers only ever get shorter (a tier can't give more time than the base timers).
 * NORMAL = the base warning/shutdown times (a simmer, boiling water); ELEVATED and
 * CRITICAL use their own, shorter ones. See docs/safety_state_machine.md.
 */
typedef enum {
    SAFETY_TIER_NORMAL = 0,
    SAFETY_TIER_ELEVATED,   /* hot (sauté, shallow frying) */
    SAFETY_TIER_CRITICAL,   /* oil heading for its smoke point */
    SAFETY_TIER_COUNT,
} safety_tier_t;

const char *safety_tier_name(safety_tier_t tier); /* "normal" "elevated" "critical" */

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
    float heat_on_temp_c;                   /* hottest-pixel temp that means cooking has started */
    float heat_off_temp_c;                  /* hysteresis: cooking considered over below this */
    float heat_on_rate_c_per_min;           /* also "cooking" when rising this fast (above heat_off); 0 = off */

    /* Presence filtering. */
    uint32_t absence_debounce_ms;           /* continuous absence needed before UNATTENDED */
    uint32_t presence_return_debounce_ms;   /* presence needed to cancel UNATTENDED/WARNING */
    uint32_t presence_return_gap_ms;        /* gaps in that presence shorter than this don't restart it; 0 = none */

    /* Timers. */
    uint32_t warning_timeout_ms;
    uint32_t shutdown_timeout_ms;
    safety_shutdown_timing_t shutdown_timing;

    /* Policies for the open questions. */
    safety_warning_exit_t warning_exit;
    safety_cooling_policy_t cooling_policy;
    uint32_t fault_shutdown_timeout_ms;     /* time in FAULT before SHUTDOWN; 0 = never */

    /*
     * Cooking judgment. cookware_tau_s <= 0 turns the trend off: any pan above
     * the cooking temperature counts as heated (the earlier behaviour).
     */
    safety_cookware_t cookware;
    float cookware_tau_s;                   /* time constant of the cookware, seconds (from the profile) */
    uint32_t trend_window_ms;               /* regression window for the temperature trend, 0 = 120000; at most
                                               SAFETY_TREND_MAX samples of 2 s */

    /*
     * Temperature tiers. A tier is off while its temperature is <= 0. Times are measured like the
     * base ones (from the start of UNATTENDED; with shutdown_timing = AFTER_WARNING_START the
     * shutdown counts from the start of the warning, i.e. tier shutdown minus tier warning).
     */
    float tier1_temp_c;                     /* ELEVATED from this hottest-pixel temperature */
    uint32_t tier1_warning_ms;
    uint32_t tier1_shutdown_ms;
    float tier2_temp_c;                     /* CRITICAL from this temperature ... */
    float tier2_rate_c_per_min;             /* ... or rising at least this fast above tier2_rate_min_temp_c (0 = off) */
    float tier2_rate_min_temp_c;
    uint32_t tier2_warning_ms;
    uint32_t tier2_shutdown_ms;

    /*
     * Pause: while someone is at the stove (presence TRUE) on an unattended timeline, the clock stops, so only
     * time with nobody there counts. A pause postpones the response, so it has a budget per timeline: at most
     * pause_budget_ms, and at most pause_budget_pct percent of the shutdown time in force. 0 = no pause.
     * Someone who stays presence_return_debounce_ms cancels the timeline instead.
     */
    uint32_t pause_budget_ms;
    uint8_t pause_budget_pct;

    /*
     * Fine tuning of the tiers, the trend and the holds. 0 = the built-in value (the #defines at the
     * top of safety.c), so a config that leaves them out behaves as before. The dashboard can set them
     * (firmware/components/rb_controller/include/rb_params.def).
     */
    uint32_t tier_up_persist_ms;            /* a hotter tier takes over after holding this long */
    uint32_t tier_down_persist_ms;          /* a cooler one only after this long ... */
    float tier_hyst_c;                      /* ... and this far below its threshold */
    uint32_t late_warning_min_ms;           /* buzzer before a shutdown that was already due */
    uint32_t trend_min_span_ms;             /* the trend needs this much history ... */
    uint16_t trend_min_samples;             /* ... and this many samples (at most SAFETY_TREND_MAX) */
    float settle_margin_c;                  /* hysteresis on the cooling decision */
    float settle_max_drift_c_per_min;       /* the prediction may climb this fast to start believing "cooling" */
    float settle_exit_drift_c_per_min;      /* ... and this fast before it stops believing it */
    uint32_t cooling_hold_max_ms;           /* a cooling hold never lasts longer than this ... */
    float cooling_hold_taus;                /* ... nor longer than this many time constants */
    uint32_t warning_min_after_hold_ms;     /* buzzer before the shutdown after any hold */

    safety_timing_hook_t timing_hook;
    void *timing_hook_ctx;
} safety_config_t;

struct safety_inputs {
    rb_tristate_t presence;                 /* UNKNOWN when the data is missing or invalid (see the header comment) */
    bool thermal_valid;
    float cooking_temp_c;                   /* hottest pixel: compared with heat_on/heat_off_temp_c */
    float temp_rate_c_per_min;              /* the thermal node's hot-region rate */
    safety_selftest_t self_test;            /* must stay PASS after SELF_TEST, otherwise FAULT */
    bool reset_request;                     /* operator reset / acknowledge (e.g. button) */
    bool safety_fault;                      /* any other safety-relevant fault (fault manager) */
};

typedef void (*safety_transition_cb_t)(safety_state_t from, safety_state_t to, const char *reason, uint32_t now_ms,
                                       void *ctx);

#define SAFETY_SETTLE_HIST 20         /* one value per 5 s: 100 s of history of the settling estimate */
#define SAFETY_TREND_MAX 128          /* 2 s samples: room for a window up to 256 s */

typedef struct {
    safety_config_t cfg;
    safety_state_t state;
    uint32_t state_entered_ms;
    safety_outputs_t outputs;

    bool heat_active;                 /* heat detector state (with hysteresis) */

    /* Trend of the hottest pixel: samples every 2 s, regression over trend_window_ms. */
    uint32_t trend_t_ms[SAFETY_TREND_MAX];
    float trend_c[SAFETY_TREND_MAX];
    uint16_t trend_head;              /* next slot to write */
    uint16_t trend_count;
    uint32_t trend_last_ms;
    float trend_rate_c_per_min;       /* NAN until there is enough data */
    float settle_c;                   /* T + tau * rate: where it is heading; NAN if unknown */
    float settle_drift_c_per_min;     /* how much settle_c rose over the last minute; NAN if unknown */
    uint32_t settle_t_ms[SAFETY_SETTLE_HIST];
    float settle_hist_c[SAFETY_SETTLE_HIST];
    uint8_t settle_head;
    uint8_t settle_count;
    uint32_t settle_last_ms;
    bool cooling;                     /* latched: heading below the cooking temperature, no heat going in */
    uint32_t cooling_since_ms;
    safety_heat_phase_t heat_phase;
    safety_hold_t hold;               /* what is holding a due transition right now */
    bool held_once;                   /* a transition was held on this timeline: WARNING lasts a minimum time */
    bool late_warning;                /* the shutdown was already due when the warning came (a tier got shorter) */

    bool paused;                      /* the clock is stopped because someone is at the stove */
    uint32_t pause_used_ms;           /* time paused on this timeline */
    uint32_t last_step_ms;
    bool have_last_step;

    safety_tier_t tier;               /* temperature tier in force */
    safety_tier_t tier_pending;       /* a different tier that has to persist before it takes over */
    uint32_t tier_pending_since_ms;
    uint32_t eff_warning_ms;          /* the warning and shutdown times in force (base, tier and hook applied) */
    uint32_t eff_shutdown_ms;
    bool return_hold_active;
    uint32_t return_hold_since_ms;
    bool presence_unknown;            /* flag: presence data missing/invalid at the last step */
    bool thermal_lost;                /* flag: thermal data missing/invalid at the last step */

    bool absent_timing;               /* absence debounce running; only a confirmed return stops it */
    uint32_t absent_since_ms;
    bool present_timing;              /* presence-return debounce running */
    uint32_t present_since_ms;
    uint32_t present_last_ms;         /* last step with presence TRUE (for the gap tolerance) */

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

/* Fill the fine-tuning fields left 0 with their built-in values, so they can be read back and shown. */
void safety_config_fill_defaults(safety_config_t *cfg);

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

/*
 * True while the supply is cut and only an operator reset restores it
 * (SHUTDOWN is latched). Published so the backend never has to infer it.
 */
bool safety_reset_required(const safety_sm_t *sm);

/* Time spent unattended so far (0 if not on the unattended timeline). */
uint32_t safety_unattended_ms(const safety_sm_t *sm, uint32_t now_ms);

/* Debounce progress for the dashboard: time since the absence began / since presence began (0 if not running). */
uint32_t safety_absent_ms(const safety_sm_t *sm, uint32_t now_ms);
uint32_t safety_returning_ms(const safety_sm_t *sm, uint32_t now_ms);

/* The pause budget in force on this timeline, in ms (0 = no pause). */
uint32_t safety_pause_budget_ms(const safety_sm_t *sm);

const char *safety_state_name(safety_state_t state);
const char *safety_heat_phase_name(safety_heat_phase_t phase); /* "off" "checking" "heating" "holding" "cooling" */
const char *safety_hold_name(safety_hold_t hold);              /* "none" "return" "cooling" */
const char *safety_buzzer_name(safety_buzzer_t buzzer);

#ifdef __cplusplus
}
#endif
