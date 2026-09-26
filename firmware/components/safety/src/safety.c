#include "safety.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* Outputs owned by each state; applied on entry. */
static const safety_outputs_t STATE_OUTPUTS[SAFETY_STATE_COUNT] = {
    [SAFETY_BOOT] = {SAFETY_BUZZER_OFF, false},
    [SAFETY_SELF_TEST] = {SAFETY_BUZZER_OFF, false},
    [SAFETY_IDLE] = {SAFETY_BUZZER_OFF, false},
    [SAFETY_MONITORING] = {SAFETY_BUZZER_OFF, false},
    [SAFETY_UNATTENDED] = {SAFETY_BUZZER_OFF, false},
    [SAFETY_WARNING] = {SAFETY_BUZZER_WARNING, false},
    [SAFETY_SHUTDOWN] = {SAFETY_BUZZER_SHUTDOWN, true},
    [SAFETY_FAULT] = {SAFETY_BUZZER_FAULT, false},
};

static bool elapsed(uint32_t now_ms, uint32_t since_ms, uint32_t timeout_ms)
{
    return (uint32_t)(now_ms - since_ms) >= timeout_ms; /* wrap-safe */
}

bool safety_config_valid(const safety_config_t *cfg)
{
    if (cfg == NULL || cfg->warning_timeout_ms == 0 || cfg->shutdown_timeout_ms == 0 || cfg->self_test_timeout_ms == 0) {
        return false;
    }
    if (cfg->shutdown_timing == SAFETY_SHUTDOWN_AFTER_UNATTENDED_START &&
        cfg->shutdown_timeout_ms <= cfg->warning_timeout_ms) {
        return false; /* shutdown would come before (or together with) the warning */
    }
    if (!isfinite(cfg->heat_on_temp_c) || !isfinite(cfg->heat_off_temp_c) || cfg->heat_off_temp_c > cfg->heat_on_temp_c) {
        return false;
    }
    if (!isfinite(cfg->heat_on_rate_c_per_min) || cfg->heat_on_rate_c_per_min < 0.0f) {
        return false;
    }
    return true;
}

/* The later of two wrap-around timestamps. */
static uint32_t later(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0 ? a : b;
}

static void clear_unattended(safety_sm_t *sm)
{
    sm->unattended_active = false;
    sm->warning_started = false;
}

/* ---- state exit / entry ---- */

static void on_exit(safety_sm_t *sm, safety_state_t from, safety_state_t to, uint32_t now_ms)
{
    (void)to;
    switch (from) {
    case SAFETY_SHUTDOWN:
        /* Only leaves through an operator reset: start the timing over. */
        clear_unattended(sm);
        if (sm->absent_timing) {
            sm->absent_since_ms = now_ms;
        }
        break;
    default:
        break;
    }
}

static void on_enter(safety_sm_t *sm, safety_state_t to, uint32_t now_ms)
{
    sm->outputs = STATE_OUTPUTS[to];
    switch (to) {
    case SAFETY_IDLE:
        clear_unattended(sm);
        break;
    case SAFETY_MONITORING:
        clear_unattended(sm);
        sm->monitoring_since_ms = now_ms;
        break;
    case SAFETY_UNATTENDED:
        if (!sm->unattended_active) {
            /*
             * The unattended time runs from when the absence started (not from
             * when the debounce confirmed it), but never from before cooking
             * was detected.
             */
            sm->unattended_active = true;
            sm->unattended_start_ms = sm->absent_timing ? later(sm->absent_since_ms, sm->monitoring_since_ms) : now_ms;
        }
        break;
    case SAFETY_WARNING:
        if (!sm->warning_started) {
            sm->warning_started = true;
            sm->warning_start_ms = now_ms;
        }
        break;
    default:
        break;
    }
}

static void transition(safety_sm_t *sm, safety_state_t to, const char *reason, uint32_t now_ms)
{
    const safety_state_t from = sm->state;
    on_exit(sm, from, to, now_ms);
    sm->state = to;
    sm->state_entered_ms = now_ms;
    sm->transitions++;
    sm->last_reason = reason;
    on_enter(sm, to, now_ms);
    if (sm->on_transition != NULL) {
        sm->on_transition(from, to, reason, now_ms, sm->cb_ctx);
    }
}

/* ---- public API ---- */

void safety_init(safety_sm_t *sm, const safety_config_t *cfg, safety_transition_cb_t cb, void *cb_ctx, uint32_t now_ms)
{
    memset(sm, 0, sizeof(*sm));
    sm->cfg = *cfg;
    sm->on_transition = cb;
    sm->cb_ctx = cb_ctx;
    sm->state = SAFETY_BOOT;
    sm->state_entered_ms = now_ms;
    sm->outputs = STATE_OUTPUTS[SAFETY_BOOT];
    sm->last_reason = "init";
}

bool safety_set_config(safety_sm_t *sm, const safety_config_t *cfg)
{
    if (!safety_config_valid(cfg)) {
        return false;
    }
    sm->cfg = *cfg;
    return true;
}

uint32_t safety_unattended_ms(const safety_sm_t *sm, uint32_t now_ms)
{
    return sm->unattended_active ? (uint32_t)(now_ms - sm->unattended_start_ms) : 0;
}

static const char *sensor_fault_reason(const safety_inputs_t *in)
{
    if (in->self_test != SAFETY_SELFTEST_PASS) {
        return "self-test not passed";
    }
    if (in->safety_fault) {
        return "safety-relevant fault active";
    }
    if (in->presence == RB_UNKNOWN) {
        return "presence data missing/invalid";
    }
    return "thermal data missing/invalid";
}

/* One evaluation pass. Returns true if a transition happened. */
static bool evaluate(safety_sm_t *sm, const safety_inputs_t *in, uint32_t now)
{
    const safety_config_t *cfg = &sm->cfg;
    /*
     * Everything the machine needs to protect the kitchen. The self-test must
     * have passed: a failed or never-completed self-test keeps FAULT latched.
     */
    const bool sensors_ok = in->presence != RB_UNKNOWN && in->thermal_valid && !in->safety_fault &&
                            in->self_test == SAFETY_SELFTEST_PASS;

    const bool absent_confirmed = sm->absent_timing && elapsed(now, sm->absent_since_ms, cfg->absence_debounce_ms);
    const bool present_confirmed =
        sm->present_timing && elapsed(now, sm->present_since_ms, cfg->presence_return_debounce_ms);

    uint32_t warning_ms = cfg->warning_timeout_ms;
    uint32_t shutdown_ms = cfg->shutdown_timeout_ms;
    if (cfg->timing_hook != NULL) {
        uint32_t w = warning_ms, s = shutdown_ms;
        cfg->timing_hook(in, &w, &s, cfg->timing_hook_ctx);
        warning_ms = w < warning_ms ? w : warning_ms;   /* the hook may only make things stricter */
        shutdown_ms = s < shutdown_ms ? s : shutdown_ms;
    }
    const bool warning_due = sm->unattended_active && elapsed(now, sm->unattended_start_ms, warning_ms);
    const bool shutdown_due = cfg->shutdown_timing == SAFETY_SHUTDOWN_AFTER_UNATTENDED_START
                                  ? sm->unattended_active && elapsed(now, sm->unattended_start_ms, shutdown_ms)
                                  : sm->warning_started && elapsed(now, sm->warning_start_ms, shutdown_ms);
    const bool cooled_down = !sm->heat_active && cfg->cooling_policy == SAFETY_COOLING_RETURN_TO_IDLE;

    switch (sm->state) {
    case SAFETY_BOOT:
        transition(sm, SAFETY_SELF_TEST, "boot complete", now);
        return true;

    case SAFETY_SELF_TEST:
        if (in->self_test == SAFETY_SELFTEST_FAIL) {
            transition(sm, SAFETY_FAULT, "self-test failed", now);
            return true;
        }
        if (in->self_test == SAFETY_SELFTEST_PASS) {
            transition(sm, SAFETY_IDLE, "self-test passed", now);
            return true;
        }
        if (elapsed(now, sm->state_entered_ms, cfg->self_test_timeout_ms)) {
            transition(sm, SAFETY_FAULT, "self-test timeout", now);
            return true;
        }
        return false;

    case SAFETY_IDLE:
        if (!sensors_ok) {
            transition(sm, SAFETY_FAULT, sensor_fault_reason(in), now);
            return true;
        }
        if (sm->heat_active) {
            transition(sm, SAFETY_MONITORING, "heating detected", now);
            return true;
        }
        return false;

    case SAFETY_MONITORING:
        if (!sensors_ok) {
            transition(sm, SAFETY_FAULT, sensor_fault_reason(in), now);
            return true;
        }
        if (!sm->heat_active) {
            transition(sm, SAFETY_IDLE, "heating stopped", now);
            return true;
        }
        if (absent_confirmed) {
            transition(sm, SAFETY_UNATTENDED, "person absent", now);
            return true;
        }
        return false;

    case SAFETY_UNATTENDED:
        if (!sensors_ok) {
            transition(sm, SAFETY_FAULT, sensor_fault_reason(in), now);
            return true;
        }
        if (present_confirmed) {
            transition(sm, SAFETY_MONITORING, "person returned", now);
            return true;
        }
        if (cooled_down) {
            transition(sm, SAFETY_IDLE, "heating stopped while unattended", now);
            return true;
        }
        if (warning_due) {
            transition(sm, SAFETY_WARNING, "unattended timeout", now);
            return true;
        }
        return false;

    case SAFETY_WARNING:
        if (!sensors_ok) {
            transition(sm, SAFETY_FAULT, sensor_fault_reason(in), now);
            return true;
        }
        if (present_confirmed && (cfg->warning_exit == SAFETY_WARNING_EXIT_ON_PRESENCE || in->reset_request)) {
            transition(sm, SAFETY_MONITORING, "person returned during warning", now);
            return true;
        }
        if (cooled_down) {
            transition(sm, SAFETY_IDLE, "heating stopped during warning", now);
            return true;
        }
        if (shutdown_due) {
            transition(sm, SAFETY_SHUTDOWN, "shutdown timeout", now);
            return true;
        }
        return false;

    case SAFETY_SHUTDOWN:
        /* Latched: only an operator reset leaves SHUTDOWN. */
        if (in->reset_request) {
            transition(sm, SAFETY_IDLE, "operator reset", now);
            return true;
        }
        return false;

    case SAFETY_FAULT:
        if (sensors_ok) {
            if (!sm->heat_active) {
                transition(sm, SAFETY_IDLE, "fault cleared, no heating", now);
            } else if (in->presence == RB_TRUE) {
                transition(sm, SAFETY_MONITORING, "fault cleared, person present", now);
            } else {
                if (!sm->unattended_active) {
                    /* Presence was unknown during the fault: count from when the fault began. */
                    sm->unattended_active = true;
                    sm->unattended_start_ms = sm->state_entered_ms;
                }
                transition(sm, SAFETY_UNATTENDED, "fault cleared, person absent", now);
            }
            return true;
        }
        /* A fault never postpones a shutdown the unattended timeline has already reached. */
        if (shutdown_due) {
            transition(sm, SAFETY_SHUTDOWN, "shutdown timeout reached during fault", now);
            return true;
        }
        if (cfg->fault_shutdown_timeout_ms > 0 && elapsed(now, sm->state_entered_ms, cfg->fault_shutdown_timeout_ms)) {
            transition(sm, SAFETY_SHUTDOWN, "fault persisted", now);
            return true;
        }
        return false;

    default:
        transition(sm, SAFETY_FAULT, "invalid state", now);
        return true;
    }
}

const safety_outputs_t *safety_step(safety_sm_t *sm, const safety_inputs_t *in, uint32_t now_ms)
{
    /* Heat detector with hysteresis. Invalid thermal data leaves it unchanged (FAULT handles that case). */
    if (in->thermal_valid && isfinite(in->hot_region_temp_c)) {
        const float t = in->hot_region_temp_c;
        const bool rising_fast = sm->cfg.heat_on_rate_c_per_min > 0.0f && isfinite(in->temp_rate_c_per_min) &&
                                 in->temp_rate_c_per_min >= sm->cfg.heat_on_rate_c_per_min &&
                                 t >= sm->cfg.heat_off_temp_c;
        sm->heat_active = sm->heat_active ? t >= sm->cfg.heat_off_temp_c : (t >= sm->cfg.heat_on_temp_c || rising_fast);
    }

    /* Presence debounce timers. UNKNOWN stops both. */
    if (in->presence == RB_FALSE) {
        if (!sm->absent_timing) {
            sm->absent_timing = true;
            sm->absent_since_ms = now_ms;
        }
    } else {
        sm->absent_timing = false;
    }
    if (in->presence == RB_TRUE) {
        if (!sm->present_timing) {
            sm->present_timing = true;
            sm->present_since_ms = now_ms;
        }
    } else {
        sm->present_timing = false;
    }

    /* Let chained transitions (e.g. BOOT -> SELF_TEST -> IDLE) settle in one step. */
    for (int i = 0; i < SAFETY_STATE_COUNT && evaluate(sm, in, now_ms); i++) {
    }
    return &sm->outputs;
}

const char *safety_state_name(safety_state_t state)
{
    static const char *const names[SAFETY_STATE_COUNT] = {
        "BOOT", "SELF_TEST", "IDLE", "MONITORING", "UNATTENDED", "WARNING", "SHUTDOWN", "FAULT",
    };
    return (unsigned)state < SAFETY_STATE_COUNT ? names[state] : "?";
}

const char *safety_buzzer_name(safety_buzzer_t buzzer)
{
    static const char *const names[] = {"OFF", "WARNING", "SHUTDOWN", "FAULT"};
    return (unsigned)buzzer < sizeof(names) / sizeof(names[0]) ? names[buzzer] : "?";
}
