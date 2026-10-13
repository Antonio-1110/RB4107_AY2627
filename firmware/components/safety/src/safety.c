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

/* Time constants of the cookware profiles (see safety.h). Estimates from typical mass and heat capacity. */
static const float COOKWARE_TAU_S[SAFETY_COOKWARE_COUNT] = {
    [SAFETY_COOKWARE_MIXED] = 600.0f,  /* 10 min: fits ordinary pans (5-20 min); pots with liquid need their own profile */
    [SAFETY_COOKWARE_LIGHT] = 300.0f,  /* 5 min  (3-8) */
    [SAFETY_COOKWARE_HEAVY] = 900.0f,  /* 15 min (10-20) */
    [SAFETY_COOKWARE_LIQUID] = 2400.0f, /* 40 min (20-60) */
};
static const char *const COOKWARE_NAMES[SAFETY_COOKWARE_COUNT] = {"mixed", "light", "heavy", "liquid"};

float safety_cookware_tau_s(safety_cookware_t profile)
{
    return (unsigned)profile < SAFETY_COOKWARE_COUNT ? COOKWARE_TAU_S[profile] : COOKWARE_TAU_S[SAFETY_COOKWARE_MIXED];
}

const char *safety_cookware_name(safety_cookware_t profile)
{
    return (unsigned)profile < SAFETY_COOKWARE_COUNT ? COOKWARE_NAMES[profile] : "?";
}

bool safety_cookware_from_name(const char *name, safety_cookware_t *out)
{
    for (int i = 0; name != NULL && i < SAFETY_COOKWARE_COUNT; i++) {
        if (strcmp(name, COOKWARE_NAMES[i]) == 0) {
            *out = (safety_cookware_t)i;
            return true;
        }
    }
    return false;
}

/* Temperature tiers: a tier takes over after its temperature has held for a few seconds (a reflection
 * must not shorten the timers), and gives way only well below its threshold and for longer. */
#define TIER_UP_PERSIST_MS 4000u
#define TIER_DOWN_PERSIST_MS 20000u
#define TIER_HYST_C 10.0f
/* A shutdown that was already due when the warning came still gives the buzzer this long. */
#define LATE_WARNING_MIN_MS 10000u

/* Trend of the hottest pixel. */
#define TREND_SAMPLE_MS 2000u          /* one sample per 2 s */
#define TREND_GAP_RESET_MS 15000u      /* a longer silence starts the trend over */
#define TREND_STALE_MS 30000u          /* no sample for this long: the trend is unknown */
#define TREND_DEFAULT_WINDOW_MS 120000u
#define TREND_MIN_SPAN_MS 60000u       /* needs at least this much history ... */
#define TREND_MIN_SAMPLES 12           /* ... and this many samples */
#define TREND_HALF_LIFE_MS 30000.0f    /* recent samples count more: a pan that was steady and now cools shows up sooner */
#define SETTLE_MARGIN_C 3.0f           /* hysteresis on the cooling decision */
/*
 * "Cooling" is only believed when the settling estimate is stable. T + tau * rate is exact
 * when the profile matches the pan: it stays put at about the room temperature. If the
 * profile's tau is longer than the pan's real one (a lowered flame whose warm-up tail is
 * still relaxing, for example), the estimate climbs as the tail fades. Rising faster than
 * this over a minute means the cooling model does not fit, and heat counts as going in.
 */
#define SETTLE_HIST_SAMPLE_MS 5000u
#define SETTLE_DRIFT_WINDOW_MS 60000u
#define SETTLE_MAX_DRIFT_C_PER_MIN 8.0f      /* to start believing "cooling" */
#define SETTLE_EXIT_DRIFT_C_PER_MIN 16.0f    /* to stop believing it (hysteresis: camera noise must not drop a hold) */
#define RISING_C_PER_MIN 0.5f          /* faster than this is "heating", otherwise "holding" */
#define COOLING_HOLD_MAX_MS 3600000u   /* a cooling hold never lasts longer than an hour ... */
#define COOLING_HOLD_TAUS 4.0f         /* ... nor longer than 4 time constants: a pan still hot after that is not just cooling */
/* After a hold, the buzzer sounds at least this long before the shutdown, even if the shutdown was due long ago. */
#define WARNING_MIN_AFTER_HOLD_MS 30000u

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
    if (cfg->pause_budget_pct > 100) {
        return false;
    }
    if (!isfinite(cfg->tier1_temp_c) || !isfinite(cfg->tier2_temp_c) || !isfinite(cfg->tier2_rate_c_per_min) ||
        !isfinite(cfg->tier2_rate_min_temp_c) || cfg->tier2_rate_c_per_min < 0.0f) {
        return false;
    }
    if (cfg->tier1_temp_c > 0.0f && cfg->tier2_temp_c > 0.0f && cfg->tier2_temp_c < cfg->tier1_temp_c) {
        return false; /* CRITICAL must not start below ELEVATED */
    }
    if (cfg->shutdown_timing == SAFETY_SHUTDOWN_AFTER_UNATTENDED_START &&
        ((cfg->tier1_temp_c > 0.0f && cfg->tier1_shutdown_ms <= cfg->tier1_warning_ms) ||
         (cfg->tier2_temp_c > 0.0f && cfg->tier2_shutdown_ms <= cfg->tier2_warning_ms))) {
        return false; /* a tier's shutdown would come before (or with) its warning */
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
    sm->held_once = false;
    sm->late_warning = false;
    sm->pause_used_ms = 0;
    sm->paused = false;
}

/* ---- temperature trend and cooking judgment ---- */

static void trend_clear(safety_sm_t *sm)
{
    sm->trend_head = 0;
    sm->trend_count = 0;
    sm->trend_last_ms = 0;
    sm->trend_rate_c_per_min = NAN;
    sm->settle_c = NAN;
    sm->settle_drift_c_per_min = NAN;
    sm->settle_count = 0;
    sm->settle_head = 0;
    sm->settle_last_ms = 0;
}

/* Remember the settling estimate every 5 s and work out how much it rose over the last minute. */
static void settle_track(safety_sm_t *sm, uint32_t now_ms)
{
    sm->settle_drift_c_per_min = NAN;
    if (!isfinite(sm->settle_c)) {
        sm->settle_count = 0; /* a gap in the trend: start the history over */
        return;
    }
    if (sm->settle_count == 0 || elapsed(now_ms, sm->settle_last_ms, SETTLE_HIST_SAMPLE_MS)) {
        sm->settle_t_ms[sm->settle_head] = now_ms;
        sm->settle_hist_c[sm->settle_head] = sm->settle_c;
        sm->settle_head = (uint8_t)((sm->settle_head + 1) % SAFETY_SETTLE_HIST);
        if (sm->settle_count < SAFETY_SETTLE_HIST) {
            sm->settle_count++;
        }
        sm->settle_last_ms = now_ms;
    }
    /* The newest remembered value that is at least a minute old. */
    for (uint8_t k = 0; k < sm->settle_count; k++) {
        const uint8_t idx = (uint8_t)((sm->settle_head + SAFETY_SETTLE_HIST - 1 - k) % SAFETY_SETTLE_HIST);
        const uint32_t age = (uint32_t)(now_ms - sm->settle_t_ms[idx]);
        if (age >= SETTLE_DRIFT_WINDOW_MS) {
            sm->settle_drift_c_per_min = (sm->settle_c - sm->settle_hist_c[idx]) * 60000.0f / (float)age;
            return;
        }
    }
}

static void trend_push(safety_sm_t *sm, uint32_t now_ms, float temp_c)
{
    if (sm->trend_count > 0 && elapsed(now_ms, sm->trend_last_ms, TREND_GAP_RESET_MS)) {
        trend_clear(sm); /* the camera was away: an old trend says nothing about now */
    }
    if (sm->trend_count > 0 && !elapsed(now_ms, sm->trend_last_ms, TREND_SAMPLE_MS)) {
        return;
    }
    sm->trend_t_ms[sm->trend_head] = now_ms;
    sm->trend_c[sm->trend_head] = temp_c;
    sm->trend_head = (uint16_t)((sm->trend_head + 1) % SAFETY_TREND_MAX);
    if (sm->trend_count < SAFETY_TREND_MAX) {
        sm->trend_count++;
    }
    sm->trend_last_ms = now_ms;
}

/*
 * Least-squares slope over the last window of samples, and from it where the
 * temperature is heading: settle = T + tau * slope (Newton cooling, dT/dt =
 * -(T - T_settle) / tau). A pan that is only cooling settles near the room
 * temperature; a pan with heat going in settles somewhere hot.
 */
static void trend_update(safety_sm_t *sm, uint32_t now_ms)
{
    const uint32_t window = sm->cfg.trend_window_ms != 0 ? sm->cfg.trend_window_ms : TREND_DEFAULT_WINDOW_MS;
    sm->trend_rate_c_per_min = NAN;
    sm->settle_c = NAN;
    if (sm->trend_count == 0 || elapsed(now_ms, sm->trend_last_ms, TREND_STALE_MS)) {
        return;
    }
    double n = 0, sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0; /* weighted sums */
    uint32_t oldest_age = 0;
    for (uint16_t k = 0; k < sm->trend_count; k++) {
        const uint16_t idx = (uint16_t)((sm->trend_head + SAFETY_TREND_MAX - 1 - k) % SAFETY_TREND_MAX);
        const uint32_t age = (uint32_t)(now_ms - sm->trend_t_ms[idx]);
        if (age > window) {
            break;
        }
        const double w = pow(0.5, (double)age / (double)TREND_HALF_LIFE_MS);
        const double x = -(double)age / 1000.0;
        const double y = sm->trend_c[idx];
        n += 1;
        sw += w;
        sx += w * x;
        sy += w * y;
        sxx += w * x * x;
        sxy += w * x * y;
        oldest_age = age;
    }
    const double denom = sw * sxx - sx * sx;
    if (n < TREND_MIN_SAMPLES || oldest_age < TREND_MIN_SPAN_MS || denom <= 0.0) {
        return;
    }
    const double slope = (sw * sxy - sx * sy) / denom; /* degC per second */
    const double now_fit = (sy - slope * sx) / sw;     /* the fitted temperature right now */
    sm->trend_rate_c_per_min = (float)(slope * 60.0);
    if (sm->cfg.cookware_tau_s > 0.0f) {
        sm->settle_c = (float)(now_fit + (double)sm->cfg.cookware_tau_s * slope);
    }
}

/* Cooking judgment: is heat going in? Updates sm->cooling and sm->heat_phase. */
static void judge_heat(safety_sm_t *sm, const safety_inputs_t *in, uint32_t now_ms)
{
    if (!sm->heat_active) {
        trend_clear(sm);
        sm->cooling = false;
        sm->heat_phase = SAFETY_HEAT_OFF;
        return;
    }
    if (in->thermal_valid && isfinite(in->cooking_temp_c)) {
        trend_push(sm, now_ms, in->cooking_temp_c);
    }
    trend_update(sm, now_ms);
    if (sm->cfg.cookware_tau_s <= 0.0f) {
        sm->cooling = false; /* trend off: heat counts as going in whenever it is hot */
        sm->heat_phase = SAFETY_HEAT_HOLDING;
        return;
    }
    if (!isfinite(sm->settle_c)) {
        sm->cooling = false; /* not enough data (or the camera is away): assume heat is going in */
        sm->heat_phase = SAFETY_HEAT_CHECKING;
        return;
    }
    settle_track(sm, now_ms);
    const float off = sm->cfg.heat_off_temp_c;
    const bool model_fits = !(isfinite(sm->settle_drift_c_per_min) && sm->settle_drift_c_per_min > SETTLE_EXIT_DRIFT_C_PER_MIN);
    if (sm->cooling) {
        if (sm->settle_c >= off + SETTLE_MARGIN_C || !model_fits) {
            sm->cooling = false; /* heading hot after all, or the estimate is climbing: the profile does not fit */
        }
    } else if (sm->settle_c < off - SETTLE_MARGIN_C && isfinite(sm->settle_drift_c_per_min) &&
               sm->settle_drift_c_per_min <= SETTLE_MAX_DRIFT_C_PER_MIN) {
        sm->cooling = true; /* below the cooking temperature, and stable: the cooling model fits */
        sm->cooling_since_ms = now_ms;
    }
    sm->heat_phase = sm->cooling ? SAFETY_HEAT_COOLING
                                 : (sm->trend_rate_c_per_min > RISING_C_PER_MIN ? SAFETY_HEAT_HEATING
                                                                                : SAFETY_HEAT_HOLDING);
}

/*
 * A due warning or shutdown can be held back, never for long:
 *  - COOLING: the pan is heading for the room temperature, so there is no heat
 *    to cut. The timers keep counting, and the transition happens as soon as
 *    the trend flattens out hot (or the pan drops below the cooking
 *    temperature, which cancels the timeline). At most 4 time constants of the
 *    cookware profile (an hour at the most): a pan that is still hot after that
 *    has heat going in, whatever the trend says.
 *  - RETURN: the radar sees someone right now and the return is being
 *    confirmed. At most presence_return_debounce_ms + presence_return_gap_ms,
 *    then it fires even if nobody is confirmed.
 */
static uint32_t cooling_hold_max_ms(const safety_sm_t *sm)
{
    const float by_tau_ms = COOLING_HOLD_TAUS * sm->cfg.cookware_tau_s * 1000.0f;
    return by_tau_ms < (float)COOLING_HOLD_MAX_MS ? (uint32_t)by_tau_ms : COOLING_HOLD_MAX_MS;
}

static safety_hold_t hold_reason(safety_sm_t *sm, const safety_inputs_t *in, uint32_t now, bool due, bool person_returned)
{
    if (!due) {
        sm->return_hold_active = false;
        return SAFETY_HOLD_NONE;
    }
    if (sm->heat_active && sm->cooling && !elapsed(now, sm->cooling_since_ms, cooling_hold_max_ms(sm))) {
        return SAFETY_HOLD_COOLING;
    }
    const bool pending = in->presence == RB_TRUE && sm->present_timing && !person_returned;
    if (!pending) {
        sm->return_hold_active = false;
        return SAFETY_HOLD_NONE;
    }
    if (!sm->return_hold_active) {
        sm->return_hold_active = true;
        sm->return_hold_since_ms = now;
    }
    const uint32_t cap = sm->cfg.presence_return_debounce_ms + sm->cfg.presence_return_gap_ms;
    return elapsed(now, sm->return_hold_since_ms, cap) ? SAFETY_HOLD_NONE : SAFETY_HOLD_RETURN;
}

/* ---- temperature tiers ---- */

static safety_tier_t tier_for(const safety_config_t *cfg, float temp_c, float rate_c_per_min, float hyst_c)
{
    const bool rising_hot = cfg->tier2_rate_c_per_min > 0.0f && isfinite(rate_c_per_min) &&
                            rate_c_per_min >= cfg->tier2_rate_c_per_min && temp_c >= cfg->tier2_rate_min_temp_c - hyst_c;
    if (cfg->tier2_temp_c > 0.0f && (temp_c >= cfg->tier2_temp_c - hyst_c || rising_hot)) {
        return SAFETY_TIER_CRITICAL;
    }
    if (cfg->tier1_temp_c > 0.0f && temp_c >= cfg->tier1_temp_c - hyst_c) {
        return SAFETY_TIER_ELEVATED;
    }
    return SAFETY_TIER_NORMAL;
}

static void update_tier(safety_sm_t *sm, const safety_inputs_t *in, uint32_t now_ms)
{
    if (!sm->heat_active) {
        sm->tier = SAFETY_TIER_NORMAL;
        sm->tier_pending = SAFETY_TIER_NORMAL;
        return;
    }
    if (!in->thermal_valid || !isfinite(in->cooking_temp_c)) {
        return; /* camera away: keep the tier we had (hot oil does not become safe because the camera stopped) */
    }
    const float t = in->cooking_temp_c, rate = sm->trend_rate_c_per_min;
    const safety_tier_t up = tier_for(&sm->cfg, t, rate, 0.0f), down = tier_for(&sm->cfg, t, rate, TIER_HYST_C);
    safety_tier_t target = sm->tier;
    uint32_t persist = 0;
    if (up > sm->tier) {
        target = up;
        persist = TIER_UP_PERSIST_MS;
    } else if (down < sm->tier) {
        target = down;
        persist = TIER_DOWN_PERSIST_MS;
    }
    if (target == sm->tier) {
        sm->tier_pending = sm->tier;
    } else if (sm->tier_pending != target) {
        sm->tier_pending = target;
        sm->tier_pending_since_ms = now_ms;
    } else if (elapsed(now_ms, sm->tier_pending_since_ms, persist)) {
        sm->tier = target;
    }
}

/* Shorten the base times with the tier's. */
static void apply_tier(const safety_sm_t *sm, uint32_t *warning_ms, uint32_t *shutdown_ms)
{
    const safety_config_t *cfg = &sm->cfg;
    uint32_t w = 0, s = 0;
    if (sm->tier == SAFETY_TIER_CRITICAL) {
        w = cfg->tier2_warning_ms;
        s = cfg->tier2_shutdown_ms;
    } else if (sm->tier == SAFETY_TIER_ELEVATED) {
        w = cfg->tier1_warning_ms;
        s = cfg->tier1_shutdown_ms;
    }
    if (w == 0 || s == 0) {
        return;
    }
    if (w < *warning_ms) {
        *warning_ms = w;
    }
    const uint32_t tier_shutdown = cfg->shutdown_timing == SAFETY_SHUTDOWN_AFTER_WARNING_START ? (s > w ? s - w : 1) : s;
    if (tier_shutdown < *shutdown_ms) {
        *shutdown_ms = tier_shutdown;
    }
}

/* ---- pause: only time with nobody at the stove counts ---- */

uint32_t safety_pause_budget_ms(const safety_sm_t *sm)
{
    uint32_t budget = sm->cfg.pause_budget_ms;
    if (sm->cfg.pause_budget_pct > 0 && sm->cfg.pause_budget_pct < 100) {
        const uint32_t shutdown_ms = sm->eff_shutdown_ms != 0 ? sm->eff_shutdown_ms : sm->cfg.shutdown_timeout_ms;
        const uint32_t by_pct = (uint32_t)(((uint64_t)shutdown_ms * sm->cfg.pause_budget_pct) / 100u);
        if (by_pct < budget) {
            budget = by_pct;
        }
    }
    return budget;
}

static void pause_clock(safety_sm_t *sm, const safety_inputs_t *in, uint32_t now_ms)
{
    uint32_t dt = sm->have_last_step ? (uint32_t)(now_ms - sm->last_step_ms) : 0;
    if (dt > 1000u) {
        dt = 1000u; /* after a stall, don't pause for the whole stall */
    }
    sm->last_step_ms = now_ms;
    sm->have_last_step = true;
    sm->paused = false;
    if ((sm->state != SAFETY_UNATTENDED && sm->state != SAFETY_WARNING) || !sm->unattended_active) {
        return;
    }
    if (in->presence != RB_TRUE) {
        return;
    }
    const uint32_t budget = safety_pause_budget_ms(sm);
    if (sm->pause_used_ms >= budget) {
        return;
    }
    if (dt > budget - sm->pause_used_ms) {
        dt = budget - sm->pause_used_ms;
    }
    sm->unattended_start_ms += dt; /* the elapsed time stands still */
    if (sm->warning_started) {
        sm->warning_start_ms += dt;
    }
    sm->pause_used_ms += dt;
    sm->paused = true;
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
    sm->trend_rate_c_per_min = NAN;
    sm->settle_c = NAN;
}

bool safety_set_config(safety_sm_t *sm, const safety_config_t *cfg)
{
    if (!safety_config_valid(cfg)) {
        return false;
    }
    sm->cfg = *cfg;
    return true;
}

bool safety_reset_required(const safety_sm_t *sm)
{
    return sm->state == SAFETY_SHUTDOWN;
}

uint32_t safety_unattended_ms(const safety_sm_t *sm, uint32_t now_ms)
{
    return sm->unattended_active ? (uint32_t)(now_ms - sm->unattended_start_ms) : 0;
}

/* FAULT is for the controller itself (self-test, relay output), never for a lost sensor. */
static bool system_ok(const safety_inputs_t *in)
{
    return !in->safety_fault && in->self_test == SAFETY_SELFTEST_PASS;
}

static const char *fault_reason(const safety_inputs_t *in)
{
    return in->self_test != SAFETY_SELFTEST_PASS ? "self-test not passed" : "safety-relevant fault active";
}

/*
 * Presence as the timers see it. UNKNOWN while cooking counts as absent (we
 * can't tell that someone is there); UNKNOWN with nothing cooking starts no
 * timer at all.
 */
static rb_tristate_t effective_presence(const safety_sm_t *sm, const safety_inputs_t *in)
{
    return in->presence == RB_UNKNOWN && sm->heat_active ? RB_FALSE : in->presence;
}

/*
 * A return only counts while the radar still sees the person: a short burst
 * (a hand wave, someone walking past) never cancels the unattended timers.
 */
static bool present_confirmed(const safety_sm_t *sm, const safety_inputs_t *in, uint32_t now)
{
    return sm->present_timing && in->presence == RB_TRUE &&
           elapsed(now, sm->present_since_ms, sm->cfg.presence_return_debounce_ms);
}

/* One evaluation pass. Returns true if a transition happened. */
static bool evaluate(safety_sm_t *sm, const safety_inputs_t *in, uint32_t now)
{
    const safety_config_t *cfg = &sm->cfg;
    /*
     * The controller itself must be sound: a failed or never-completed
     * self-test, or a failed shutdown output, keeps FAULT latched. A lost
     * sensor does not (see the flags in safety_step()).
     */
    const bool sensors_ok = system_ok(in);

    const bool absent_confirmed = sm->absent_timing && elapsed(now, sm->absent_since_ms, cfg->absence_debounce_ms);
    const bool person_returned = present_confirmed(sm, in, now);

    uint32_t warning_ms = cfg->warning_timeout_ms;
    uint32_t shutdown_ms = cfg->shutdown_timeout_ms;
    apply_tier(sm, &warning_ms, &shutdown_ms);
    if (cfg->timing_hook != NULL) {
        uint32_t w = warning_ms, s = shutdown_ms;
        cfg->timing_hook(in, &w, &s, cfg->timing_hook_ctx);
        warning_ms = w < warning_ms ? w : warning_ms;   /* the hook may only make things stricter */
        shutdown_ms = s < shutdown_ms ? s : shutdown_ms;
    }
    sm->eff_warning_ms = warning_ms;
    sm->eff_shutdown_ms = shutdown_ms;
    const bool warning_due = sm->unattended_active && elapsed(now, sm->unattended_start_ms, warning_ms);
    const bool shutdown_due = cfg->shutdown_timing == SAFETY_SHUTDOWN_AFTER_UNATTENDED_START
                                  ? sm->unattended_active && elapsed(now, sm->unattended_start_ms, shutdown_ms)
                                  : sm->warning_started && elapsed(now, sm->warning_start_ms, shutdown_ms);
    const bool cooled_down = !sm->heat_active && cfg->cooling_policy == SAFETY_COOLING_RETURN_TO_IDLE;

    sm->hold = SAFETY_HOLD_NONE;

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
            transition(sm, SAFETY_FAULT, fault_reason(in), now);
            return true;
        }
        if (sm->heat_active) {
            transition(sm, SAFETY_MONITORING, "heating detected", now);
            return true;
        }
        return false;

    case SAFETY_MONITORING:
        if (!sensors_ok) {
            transition(sm, SAFETY_FAULT, fault_reason(in), now);
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
            transition(sm, SAFETY_FAULT, fault_reason(in), now);
            return true;
        }
        if (person_returned) {
            transition(sm, SAFETY_MONITORING, "person returned", now);
            return true;
        }
        if (cooled_down) {
            transition(sm, SAFETY_IDLE, "heating stopped while unattended", now);
            return true;
        }
        sm->hold = hold_reason(sm, in, now, warning_due, person_returned);
        sm->held_once = sm->held_once || sm->hold != SAFETY_HOLD_NONE;
        if (warning_due && sm->hold == SAFETY_HOLD_NONE) {
            sm->late_warning = shutdown_due; /* a tier got shorter while nobody was there */
            transition(sm, SAFETY_WARNING, "unattended timeout", now);
            return true;
        }
        return false;

    case SAFETY_WARNING:
        if (!sensors_ok) {
            transition(sm, SAFETY_FAULT, fault_reason(in), now);
            return true;
        }
        if (person_returned && (cfg->warning_exit == SAFETY_WARNING_EXIT_ON_PRESENCE || in->reset_request)) {
            transition(sm, SAFETY_MONITORING, "person returned during warning", now);
            return true;
        }
        if (cooled_down) {
            transition(sm, SAFETY_IDLE, "heating stopped during warning", now);
            return true;
        }
        sm->hold = hold_reason(sm, in, now, shutdown_due, person_returned);
        sm->held_once = sm->held_once || sm->hold != SAFETY_HOLD_NONE;
        /* A timeline that was held, or whose limits got shorter, does not jump straight to the shutdown: the buzzer sounds first. */
        const uint32_t dwell_ms = sm->held_once ? WARNING_MIN_AFTER_HOLD_MS : (sm->late_warning ? LATE_WARNING_MIN_MS : 0);
        if (shutdown_due && sm->hold == SAFETY_HOLD_NONE && elapsed(now, sm->state_entered_ms, dwell_ms)) {
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
                    /* Nobody seen (or presence unknown while cooking): count from when the fault began. */
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
    /*
     * Heat detector with hysteresis on the hottest pixel (the same number the
     * dashboard outlines). Invalid thermal data leaves it unchanged: a camera
     * dropout neither starts nor cancels cooking, it only raises thermal_lost.
     */
    if (in->thermal_valid && isfinite(in->cooking_temp_c)) {
        const float t = in->cooking_temp_c;
        const bool rising_fast = sm->cfg.heat_on_rate_c_per_min > 0.0f && isfinite(in->temp_rate_c_per_min) &&
                                 in->temp_rate_c_per_min >= sm->cfg.heat_on_rate_c_per_min &&
                                 t >= sm->cfg.heat_off_temp_c;
        sm->heat_active = sm->heat_active ? t >= sm->cfg.heat_off_temp_c : (t >= sm->cfg.heat_on_temp_c || rising_fast);
    }
    judge_heat(sm, in, now_ms);
    update_tier(sm, in, now_ms);
    pause_clock(sm, in, now_ms);
    sm->presence_unknown = in->presence == RB_UNKNOWN;
    sm->thermal_lost = !in->thermal_valid;

    /*
     * Return timer: runs while the radar sees someone and survives "absent"
     * gaps shorter than presence_return_gap_ms (radar flicker from a person
     * who is really there). UNKNOWN stops it.
     */
    if (in->presence == RB_TRUE) {
        if (!sm->present_timing) {
            sm->present_timing = true;
            sm->present_since_ms = now_ms;
        }
        sm->present_last_ms = now_ms;
    } else if (in->presence == RB_UNKNOWN || elapsed(now_ms, sm->present_last_ms, sm->cfg.presence_return_gap_ms)) {
        sm->present_timing = false;
    }

    /*
     * Absence timer. It starts at the first "absent" and a presence blip does
     * not restart it: only a confirmed return (above) stops it, so radar noise
     * can't keep the absence from ever being confirmed. UNKNOWN with nothing
     * cooking starts no timer; UNKNOWN while cooking counts as absent.
     */
    const rb_tristate_t eff = effective_presence(sm, in);
    if (present_confirmed(sm, in, now_ms)) {
        sm->absent_timing = false;
    } else if (eff == RB_FALSE) {
        if (!sm->absent_timing) {
            sm->absent_timing = true;
            sm->absent_since_ms = now_ms;
        }
    } else if (eff == RB_UNKNOWN) {
        sm->absent_timing = false;
    }

    /* Let chained transitions (e.g. BOOT -> SELF_TEST -> IDLE) settle in one step. */
    for (int i = 0; i < SAFETY_STATE_COUNT && evaluate(sm, in, now_ms); i++) {
    }
    return &sm->outputs;
}

uint32_t safety_absent_ms(const safety_sm_t *sm, uint32_t now_ms)
{
    return sm->absent_timing ? (uint32_t)(now_ms - sm->absent_since_ms) : 0;
}

uint32_t safety_returning_ms(const safety_sm_t *sm, uint32_t now_ms)
{
    return sm->present_timing ? (uint32_t)(now_ms - sm->present_since_ms) : 0;
}

const char *safety_state_name(safety_state_t state)
{
    static const char *const names[SAFETY_STATE_COUNT] = {
        "BOOT", "SELF_TEST", "IDLE", "MONITORING", "UNATTENDED", "WARNING", "SHUTDOWN", "FAULT",
    };
    return (unsigned)state < SAFETY_STATE_COUNT ? names[state] : "?";
}

const char *safety_tier_name(safety_tier_t tier)
{
    static const char *const names[] = {"normal", "elevated", "critical"};
    return (unsigned)tier < sizeof(names) / sizeof(names[0]) ? names[tier] : "?";
}

const char *safety_heat_phase_name(safety_heat_phase_t phase)
{
    static const char *const names[] = {"off", "checking", "heating", "holding", "cooling"};
    return (unsigned)phase < sizeof(names) / sizeof(names[0]) ? names[phase] : "?";
}

const char *safety_hold_name(safety_hold_t hold)
{
    static const char *const names[] = {"none", "return", "cooling"};
    return (unsigned)hold < sizeof(names) / sizeof(names[0]) ? names[hold] : "?";
}

const char *safety_buzzer_name(safety_buzzer_t buzzer)
{
    static const char *const names[] = {"OFF", "WARNING", "SHUTDOWN", "FAULT"};
    return (unsigned)buzzer < sizeof(names) / sizeof(names[0]) ? names[buzzer] : "?";
}
