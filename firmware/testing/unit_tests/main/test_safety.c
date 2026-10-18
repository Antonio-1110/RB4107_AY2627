/* Safety state machine tests: every scenario in TODO section 28 plus edge cases. */
#include <math.h>
#include <stdint.h>
#include "safety.h"
#include "test_harness.h"
#include "unity.h"

safety_config_t th_default_config(void)
{
    return (safety_config_t){
        .self_test_timeout_ms = 5000,
        .heat_on_temp_c = 50.0f,
        .heat_off_temp_c = 40.0f,
        .heat_on_rate_c_per_min = 0.0f,
        .absence_debounce_ms = 2000,
        .presence_return_debounce_ms = 0,
        .warning_timeout_ms = 60000,
        .shutdown_timeout_ms = 90000,
        .shutdown_timing = SAFETY_SHUTDOWN_AFTER_UNATTENDED_START,
        .warning_exit = SAFETY_WARNING_EXIT_ON_PRESENCE,
        .cooling_policy = SAFETY_COOLING_KEEP_TIMERS,
        .fault_shutdown_timeout_ms = 90000,
    };
}

void th_start(th_t *h, const safety_config_t *cfg, uint32_t t0)
{
    TEST_ASSERT_TRUE(safety_config_valid(cfg));
    h->now = t0;
    safety_init(&h->sm, cfg, NULL, NULL, h->now);
    h->in = (safety_inputs_t){
        .presence = RB_TRUE,
        .thermal_valid = true,
        .cooking_temp_c = 25.0f,
        .temp_rate_c_per_min = 0.0f,
        .self_test = SAFETY_SELFTEST_PASS,
    };
    th_run(h, TH_STEP_MS);
    TEST_ASSERT_EQUAL_STRING("IDLE", safety_state_name(h->sm.state));
}

void th_run(th_t *h, uint32_t ms)
{
    for (uint32_t end = h->now + ms; (int32_t)(end - h->now) > 0;) {
        h->now += TH_STEP_MS;
        h->out = *safety_step(&h->sm, &h->in, h->now);
    }
}

void th_cook(th_t *h)
{
    h->in.cooking_temp_c = 120.0f;
    th_run(h, TH_STEP_MS);
    TH_EXPECT(h, SAFETY_MONITORING);
}

/* Leave, and run until just after the absence debounce confirmed it. */
static void leave(th_t *h)
{
    h->in.presence = RB_FALSE;
    th_run(h, 2000 + TH_STEP_MS);
    TH_EXPECT(h, SAFETY_UNATTENDED);
}

/* ---- boot / self-test ---- */

static void test_boot_selftest_pass_to_idle(void)
{
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    TEST_ASSERT_EQUAL(SAFETY_BUZZER_OFF, h.out.buzzer);
    TEST_ASSERT_FALSE(h.out.shutdown);
}

static void test_boot_selftest_fail_or_timeout_to_fault(void)
{
    th_t h = {0};
    const safety_config_t cfg = th_default_config();
    safety_init(&h.sm, &cfg, NULL, NULL, 0);
    h.in = (safety_inputs_t){.presence = RB_TRUE, .thermal_valid = true, .cooking_temp_c = 25, .self_test = SAFETY_SELFTEST_FAIL};
    th_run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_FAULT);

    safety_init(&h.sm, &cfg, NULL, NULL, 0);
    h.now = 0;
    h.in.self_test = SAFETY_SELFTEST_PENDING;
    th_run(&h, cfg.self_test_timeout_ms - TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_SELF_TEST);
    th_run(&h, 2 * TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_FAULT);
}

/* ---- the transitions required by TODO section 28 ---- */

static void test_idle_to_monitoring(void)
{
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    h.in.cooking_temp_c = 49.9f;
    th_run(&h, 1000);
    TH_EXPECT(&h, SAFETY_IDLE);
    h.in.cooking_temp_c = 50.0f;
    th_run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_MONITORING);
    /* Hysteresis: stays until below heat_off. */
    h.in.cooking_temp_c = 45.0f;
    th_run(&h, 1000);
    TH_EXPECT(&h, SAFETY_MONITORING);
    h.in.cooking_temp_c = 39.0f;
    th_run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_IDLE);
}

static void test_monitoring_to_unattended_after_debounce(void)
{
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    h.in.presence = RB_FALSE;
    th_run(&h, cfg.absence_debounce_ms - TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_MONITORING);
    th_run(&h, 2 * TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    /* Timer counts from when the absence began, not from the debounce. */
    TEST_ASSERT_UINT32_WITHIN(TH_STEP_MS, cfg.absence_debounce_ms + TH_STEP_MS, safety_unattended_ms(&h.sm, h.now));
}

static void test_unattended_to_monitoring_person_returns(void)
{
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    leave(&h);
    th_run(&h, 30000);
    h.in.presence = RB_TRUE;
    th_run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_MONITORING);
    TEST_ASSERT_EQUAL_UINT32(0, safety_unattended_ms(&h.sm, h.now));
    /* Leaving again starts a fresh timer: no warning after only 40 s more. */
    leave(&h);
    th_run(&h, 40000);
    TH_EXPECT(&h, SAFETY_UNATTENDED);
}

static void test_unattended_to_warning(void)
{
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    const uint32_t left_at = h.now;
    h.in.presence = RB_FALSE;
    th_run(&h, cfg.warning_timeout_ms - TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    th_run(&h, 2 * TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_WARNING);
    TEST_ASSERT_EQUAL(SAFETY_BUZZER_WARNING, h.out.buzzer);
    TEST_ASSERT_FALSE(h.out.shutdown);
    TEST_ASSERT_UINT32_WITHIN(2 * TH_STEP_MS, cfg.warning_timeout_ms, h.sm.warning_start_ms - left_at);
}

static void test_warning_to_shutdown_total_time(void)
{
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    h.in.presence = RB_FALSE;
    th_run(&h, cfg.shutdown_timeout_ms - TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_WARNING);
    TEST_ASSERT_FALSE(safety_reset_required(&h.sm));
    th_run(&h, 2 * TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_SHUTDOWN);
    TEST_ASSERT_TRUE(h.out.shutdown);
    TEST_ASSERT_TRUE(safety_reset_required(&h.sm));
    TEST_ASSERT_EQUAL(SAFETY_BUZZER_SHUTDOWN, h.out.buzzer);

    /* Latched: the person coming back doesn't release it; only a reset does. */
    h.in.presence = RB_TRUE;
    th_run(&h, 10000);
    TH_EXPECT(&h, SAFETY_SHUTDOWN);
    h.in.reset_request = true;
    th_run(&h, TH_STEP_MS);
    h.in.reset_request = false;
    TH_EXPECT(&h, SAFETY_MONITORING); /* IDLE, then straight back to MONITORING: the hob is still hot */
    TEST_ASSERT_FALSE(h.out.shutdown);
    TEST_ASSERT_FALSE(safety_reset_required(&h.sm));
}

static void test_shutdown_timing_after_warning_mode(void)
{
    th_t h;
    safety_config_t cfg = th_default_config();
    cfg.shutdown_timing = SAFETY_SHUTDOWN_AFTER_WARNING_START;
    th_start(&h, &cfg, 0);
    th_cook(&h);
    h.in.presence = RB_FALSE;
    th_run(&h, cfg.warning_timeout_ms + cfg.shutdown_timeout_ms - TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_WARNING);
    th_run(&h, 3 * TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_SHUTDOWN);
}

static void test_person_returns_during_warning(void)
{
    th_t h;
    safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    h.in.presence = RB_FALSE;
    th_run(&h, cfg.warning_timeout_ms + 5000);
    TH_EXPECT(&h, SAFETY_WARNING);
    h.in.presence = RB_TRUE;
    th_run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_MONITORING);
    TEST_ASSERT_EQUAL(SAFETY_BUZZER_OFF, h.out.buzzer);

    /* Policy: acknowledgement required. */
    cfg.warning_exit = SAFETY_WARNING_EXIT_ON_ACK;
    th_start(&h, &cfg, 0);
    th_cook(&h);
    h.in.presence = RB_FALSE;
    th_run(&h, cfg.warning_timeout_ms + 5000);
    h.in.presence = RB_TRUE;
    th_run(&h, 5000);
    TH_EXPECT(&h, SAFETY_WARNING);
    h.in.reset_request = true;
    th_run(&h, TH_STEP_MS);
    h.in.reset_request = false;
    TH_EXPECT(&h, SAFETY_MONITORING);
}

static void test_temperature_drops_during_unattended(void)
{
    th_t h;
    safety_config_t cfg = th_default_config();
    /* Default policy: keep the timers (don't trust a cooling reading to mean "off"). */
    th_start(&h, &cfg, 0);
    th_cook(&h);
    leave(&h);
    h.in.cooking_temp_c = 30.0f;
    th_run(&h, cfg.shutdown_timeout_ms);
    TH_EXPECT(&h, SAFETY_SHUTDOWN);

    cfg.cooling_policy = SAFETY_COOLING_RETURN_TO_IDLE;
    th_start(&h, &cfg, 0);
    th_cook(&h);
    leave(&h);
    h.in.cooking_temp_c = 30.0f;
    th_run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_IDLE);
    TEST_ASSERT_EQUAL_UINT32(0, safety_unattended_ms(&h.sm, h.now));
}

/* ---- lost sensors: flags, not FAULT ---- */

static void test_presence_unknown_no_heat_flag_only(void)
{
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    h.in.presence = RB_UNKNOWN;
    th_run(&h, cfg.shutdown_timeout_ms + 10000);
    TH_EXPECT(&h, SAFETY_IDLE); /* no FAULT, no shutdown */
    TEST_ASSERT_TRUE(h.sm.presence_unknown);
    TEST_ASSERT_FALSE(h.sm.thermal_lost);
    TEST_ASSERT_FALSE(h.sm.absent_timing); /* no timer */
    TEST_ASSERT_FALSE(h.sm.unattended_active);
    TEST_ASSERT_EQUAL(SAFETY_BUZZER_OFF, h.out.buzzer);
    h.in.presence = RB_TRUE;
    th_run(&h, TH_STEP_MS);
    TEST_ASSERT_FALSE(h.sm.presence_unknown);
}

static void test_presence_unknown_while_cooking_runs_timeline(void)
{
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    h.in.presence = RB_UNKNOWN; /* the radar dies while the hob is hot */
    th_run(&h, cfg.absence_debounce_ms + TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    TEST_ASSERT_TRUE(h.sm.presence_unknown);
    th_run(&h, cfg.warning_timeout_ms);
    TH_EXPECT(&h, SAFETY_WARNING);
    th_run(&h, cfg.shutdown_timeout_ms - cfg.warning_timeout_ms);
    TH_EXPECT(&h, SAFETY_SHUTDOWN);
}

static void test_presence_unknown_then_confirmed_return_cancels(void)
{
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    h.in.presence = RB_UNKNOWN;
    th_run(&h, 30000);
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    h.in.presence = RB_TRUE;
    th_run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_MONITORING);
}

static void test_presence_unknown_keeps_running_timeline(void)
{
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    h.in.presence = RB_FALSE;
    th_run(&h, 50000); /* 50 s unattended */
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    h.in.presence = RB_UNKNOWN; /* C4002 dies: the timeline is not restarted or postponed */
    th_run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    th_run(&h, cfg.shutdown_timeout_ms - 50000 + TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_SHUTDOWN); /* still at 90 s total, not 50 + 90 */
}

static void test_thermal_lost_flag_only(void)
{
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    h.in.thermal_valid = false; /* no cooking invented: IDLE stays IDLE */
    h.in.presence = RB_FALSE;
    th_run(&h, cfg.shutdown_timeout_ms + 10000);
    TH_EXPECT(&h, SAFETY_IDLE);
    TEST_ASSERT_TRUE(h.sm.thermal_lost);
    TEST_ASSERT_FALSE(h.sm.presence_unknown);
    TEST_ASSERT_FALSE(h.out.shutdown);
    h.in.thermal_valid = true;
    th_run(&h, TH_STEP_MS);
    TEST_ASSERT_FALSE(h.sm.thermal_lost);
}

static void test_thermal_lost_keeps_running_timeline(void)
{
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    h.in.presence = RB_FALSE;
    th_run(&h, 30000);
    h.in.thermal_valid = false; /* camera dropout for 10 s: no FAULT, timeline keeps counting */
    th_run(&h, 10000);
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    TEST_ASSERT_TRUE(h.sm.thermal_lost);
    h.in.thermal_valid = true;
    th_run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    TEST_ASSERT_UINT32_WITHIN(500, 40000, safety_unattended_ms(&h.sm, h.now));
}

static void test_thermal_lost_with_person_present_stays_monitoring(void)
{
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    h.in.thermal_valid = false;
    th_run(&h, cfg.shutdown_timeout_ms + 10000);
    TH_EXPECT(&h, SAFETY_MONITORING);
}

static void test_fault_only_for_controller_faults(void)
{
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    h.in.safety_fault = true; /* e.g. relay read-back failure */
    th_run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_FAULT);
    /* Both sensors lost on top: FAULT still ends when the controller fault clears. */
    h.in.presence = RB_UNKNOWN;
    h.in.thermal_valid = false;
    h.in.safety_fault = false;
    th_run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_UNATTENDED); /* heat was active, presence unknown = absent */
}

static void test_safety_fault_input(void)
{
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    h.in.safety_fault = true; /* e.g. relay read-back failure */
    th_run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_FAULT);
}

static void test_reset_restarts_timing(void)
{
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    h.in.presence = RB_FALSE;
    th_run(&h, cfg.shutdown_timeout_ms + 1000);
    TH_EXPECT(&h, SAFETY_SHUTDOWN);
    /* Reset while the sensor still says absent: must not trip again at once. */
    h.in.reset_request = true;
    th_run(&h, TH_STEP_MS);
    h.in.reset_request = false;
    th_run(&h, cfg.warning_timeout_ms - 5000);
    TEST_ASSERT_TRUE(h.sm.state == SAFETY_MONITORING || h.sm.state == SAFETY_UNATTENDED);
}

static void test_heat_detected_after_person_left(void)
{
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    h.in.presence = RB_FALSE; /* nobody there for 10 minutes, hob cold */
    th_run(&h, 600000);
    TH_EXPECT(&h, SAFETY_IDLE);
    h.in.cooking_temp_c = 120.0f;
    th_run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_UNATTENDED); /* cooking with nobody there */
    TEST_ASSERT_TRUE(safety_unattended_ms(&h.sm, h.now) <= TH_STEP_MS); /* counts from now, not 10 min ago */
}

static void test_rate_threshold_optional(void)
{
    th_t h;
    safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    h.in.cooking_temp_c = 45.0f;
    h.in.temp_rate_c_per_min = 20.0f;
    th_run(&h, 1000);
    TH_EXPECT(&h, SAFETY_IDLE); /* rate trigger off by default */
    cfg.heat_on_rate_c_per_min = 10.0f;
    th_start(&h, &cfg, 0);
    h.in.cooking_temp_c = 45.0f;
    h.in.temp_rate_c_per_min = 20.0f;
    th_run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_MONITORING);
}

static void halve(const safety_inputs_t *in, uint32_t *w, uint32_t *s, void *ctx)
{
    *w /= 2;
    *s *= 10; /* trying to lengthen: must be ignored */
}

static void test_timing_hook_can_only_shorten(void)
{
    th_t h;
    safety_config_t cfg = th_default_config();
    cfg.timing_hook = halve;
    th_start(&h, &cfg, 0);
    th_cook(&h);
    h.in.presence = RB_FALSE;
    th_run(&h, cfg.warning_timeout_ms / 2 + TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_WARNING);
    th_run(&h, cfg.shutdown_timeout_ms - cfg.warning_timeout_ms / 2);
    TH_EXPECT(&h, SAFETY_SHUTDOWN);
}

static void test_monotonic_wraparound(void)
{
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, UINT32_MAX - 30000); /* the 32-bit ms counter wraps mid-scenario */
    th_cook(&h);
    h.in.presence = RB_FALSE;
    th_run(&h, cfg.warning_timeout_ms + TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_WARNING);
}

static void test_config_validation(void)
{
    safety_config_t cfg = th_default_config();
    TEST_ASSERT_TRUE(safety_config_valid(&cfg));
    cfg.shutdown_timeout_ms = cfg.warning_timeout_ms; /* shutdown not after warning */
    TEST_ASSERT_FALSE(safety_config_valid(&cfg));
    cfg = th_default_config();
    cfg.heat_off_temp_c = 60.0f; /* off above on */
    TEST_ASSERT_FALSE(safety_config_valid(&cfg));
    cfg = th_default_config();
    cfg.heat_on_temp_c = NAN;
    TEST_ASSERT_FALSE(safety_config_valid(&cfg));
}

/* ---- presence-return filter (a hand wave must not reset the timers) ---- */

static safety_config_t return_filter_config(void)
{
    safety_config_t cfg = th_default_config();
    cfg.presence_return_debounce_ms = 3000;
    cfg.presence_return_gap_ms = 1000;
    return cfg;
}

/* Presence for `on` ms, then absent for `off` ms. */
static void blip(th_t *h, uint32_t on, uint32_t off)
{
    h->in.presence = RB_TRUE;
    th_run(h, on);
    h->in.presence = RB_FALSE;
    th_run(h, off);
}

static void test_wave_keeps_unattended_timer(void)
{
    th_t h;
    const safety_config_t cfg = return_filter_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    leave(&h);
    th_run(&h, 20000);
    const uint32_t before = safety_unattended_ms(&h.sm, h.now);
    blip(&h, 2000, 2000); /* a wave: ~2 s seen, then gone */
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    TEST_ASSERT_UINT32_WITHIN(TH_STEP_MS, before + 4000, safety_unattended_ms(&h.sm, h.now));
    /* The warning still comes 60 s after the person left. */
    th_run(&h, cfg.warning_timeout_ms - safety_unattended_ms(&h.sm, h.now) - TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    th_run(&h, 2 * TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_WARNING);
}

static void test_wave_during_warning_keeps_shutdown(void)
{
    th_t h;
    const safety_config_t cfg = return_filter_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    leave(&h);
    th_run(&h, cfg.warning_timeout_ms);
    TH_EXPECT(&h, SAFETY_WARNING);
    blip(&h, 2500, 1500);
    TH_EXPECT(&h, SAFETY_WARNING);
    th_run(&h, cfg.shutdown_timeout_ms - safety_unattended_ms(&h.sm, h.now) + TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_SHUTDOWN);
}

static void test_return_survives_short_flicker(void)
{
    th_t h;
    const safety_config_t cfg = return_filter_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    leave(&h);
    th_run(&h, 10000);
    blip(&h, 1500, 500); /* flicker shorter than the 1 s gap */
    h.in.presence = RB_TRUE;
    th_run(&h, 900);
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    th_run(&h, 200); /* 3 s since the return began */
    TH_EXPECT(&h, SAFETY_MONITORING);
}

static void test_long_gap_restarts_return_count(void)
{
    th_t h;
    const safety_config_t cfg = return_filter_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    leave(&h);
    th_run(&h, 10000);
    blip(&h, 2000, 1500); /* gap longer than 1 s: the count starts again */
    h.in.presence = RB_TRUE;
    th_run(&h, 2000);
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    th_run(&h, 1000 + TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_MONITORING);
}

static void test_return_needs_presence_when_count_completes(void)
{
    th_t h;
    const safety_config_t cfg = return_filter_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    leave(&h);
    th_run(&h, 10000);
    blip(&h, 2500, 900); /* the 3 s pass inside the gap: not a return */
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    th_run(&h, 2000);
    TH_EXPECT(&h, SAFETY_UNATTENDED);
}

/* ---- radar noise: blips must not stop the absence from being confirmed ---- */

static void test_presence_blips_do_not_block_absence(void)
{
    th_t h;
    safety_config_t cfg = return_filter_config();
    cfg.absence_debounce_ms = 1000;
    th_start(&h, &cfg, 0);
    th_cook(&h);
    h.in.presence = RB_FALSE;
    /*
     * Really absent, but a 200 ms "present" blip every 800 ms: the absence never lasts 1 s without
     * a blip (the old edge timer never confirmed it), yet it is not a 3 s return either.
     */
    for (int i = 0; i < 3; i++) {
        h.in.presence = RB_TRUE;
        th_run(&h, 200);
        h.in.presence = RB_FALSE;
        th_run(&h, 600);
    }
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    TEST_ASSERT_TRUE(safety_unattended_ms(&h.sm, h.now) > 0);
}

static void test_blip_inside_debounce_keeps_absent_since(void)
{
    th_t h;
    safety_config_t cfg = return_filter_config();
    cfg.absence_debounce_ms = 2000;
    th_start(&h, &cfg, 0);
    th_cook(&h);
    h.in.presence = RB_FALSE;
    th_run(&h, 1000);
    h.in.presence = RB_TRUE; /* one 100 ms blip must not restart the 2 s debounce */
    th_run(&h, TH_STEP_MS);
    h.in.presence = RB_FALSE;
    th_run(&h, 1000);
    TH_EXPECT(&h, SAFETY_UNATTENDED);
}

static void test_real_return_after_noise_cancels_timer(void)
{
    th_t h;
    safety_config_t cfg = return_filter_config();
    cfg.absence_debounce_ms = 1000;
    th_start(&h, &cfg, 0);
    th_cook(&h);
    h.in.presence = RB_FALSE;
    th_run(&h, 1000 + TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    h.in.presence = RB_TRUE; /* sustained >= 3 s: a real return */
    th_run(&h, 3000 + TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_MONITORING);
    TEST_ASSERT_FALSE(h.sm.absent_timing);
    /* leaving again starts a fresh absence */
    h.in.presence = RB_FALSE;
    th_run(&h, 1000 + TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    TEST_ASSERT_UINT32_WITHIN(300, 1100, safety_unattended_ms(&h.sm, h.now)); /* counted from the new absence */
}

/* ---- cooking judgment: cooling pan vs heat going in ---- */

static safety_config_t cookware_config(safety_cookware_t profile)
{
    safety_config_t cfg = th_default_config();
    cfg.cooling_policy = SAFETY_COOLING_RETURN_TO_IDLE;
    cfg.warning_timeout_ms = 120000;
    cfg.shutdown_timeout_ms = 240000;
    cfg.absence_debounce_ms = 2000;
    cfg.cookware = profile;
    cfg.cookware_tau_s = safety_cookware_tau_s(profile);
    return cfg;
}

typedef float (*curve_fn)(float t_s);

/* The curve's time zero; curve_start() sets it, consecutive run_curve() calls continue the same curve. */
static uint32_t s_curve_t0;
static void curve_start(const th_t *h) { s_curve_t0 = h->now; }

/* Drive the hottest pixel along a curve of the time since curve_start(). */
static void run_curve(th_t *h, uint32_t ms, curve_fn curve)
{
    for (uint32_t end = h->now + ms; (int32_t)(end - h->now) > 0;) {
        h->in.cooking_temp_c = curve((float)(h->now - s_curve_t0) / 1000.0f);
        th_run(h, TH_STEP_MS);
    }
}

static float steady_150(float t) { (void)t; return 150.0f; }
/* Burner off, light pan (tau 5 min) cooling to a 25 degC room. */
static float off_light(float t) { return 25.0f + 125.0f * expf(-t / 300.0f); }
/* Burner off, a pot of soup (tau 40 min). */
static float off_liquid(float t) { return 25.0f + 125.0f * expf(-t / 2400.0f); }
/* Burner turned down to low: settles at 90 degC with the pan's tau 5 min. */
static float low_flame(float t) { return 90.0f + 60.0f * expf(-t / 300.0f); }
/* Turned on low while still at about 88 degC: rises a little and settles at 90. */
static float low_from_88(float t) { return 90.0f - 2.0f * expf(-t / 300.0f); }
/* Boiling: holds 100 degC with a little noise. */
static float boiling(float t) { return 100.0f + 0.3f * sinf(t * 3.1f); }
/* The person leaves while the burner is on: a long steady cook, then the pan is left alone. */
static void cook_then_leave(th_t *h)
{
    th_cook(h);
    curve_start(h);
    run_curve(h, 150000, steady_150);
    TH_EXPECT(h, SAFETY_MONITORING);
    h->in.presence = RB_FALSE;
    curve_start(h); /* the next curve starts the moment the person leaves */
}

static void test_cooling_pan_holds_the_timers_then_cancels(void)
{
    th_t h;
    const safety_config_t cfg = cookware_config(SAFETY_COOKWARE_LIGHT);
    th_start(&h, &cfg, 0);
    cook_then_leave(&h);
    /* Burner off: the pan cools by itself. The timers must never fire... */
    for (int minute = 0; minute < 10; minute++) {
        run_curve(&h, 60000, off_light);
        TEST_ASSERT_TRUE(h.sm.state != SAFETY_WARNING && h.sm.state != SAFETY_SHUTDOWN);
    }
    /* ...and once it is below the cooking temperature the timeline is cancelled. */
    h.in.cooking_temp_c = 30.0f;
    th_run(&h, 5000);
    TH_EXPECT(&h, SAFETY_IDLE);
}

static void test_cooling_hold_is_reported(void)
{
    th_t h;
    const safety_config_t cfg = cookware_config(SAFETY_COOKWARE_LIGHT);
    th_start(&h, &cfg, 0);
    cook_then_leave(&h);
    bool saw_hold = false;
    const uint32_t t0 = h.now;
    while (h.now - t0 < 400000) {
        h.in.cooking_temp_c = off_light((float)(h.now - t0) / 1000.0f);
        th_run(&h, TH_STEP_MS);
        saw_hold = saw_hold || h.sm.hold == SAFETY_HOLD_COOLING;
    }
    TEST_ASSERT_TRUE(saw_hold); /* the warning came due while it was cooling */
    TEST_ASSERT_EQUAL(SAFETY_HEAT_COOLING, h.sm.heat_phase);
    TEST_ASSERT_TRUE(h.sm.settle_c < 40.0f);
    TEST_ASSERT_TRUE(h.sm.trend_rate_c_per_min < 0.0f);
}

static void test_slow_cooling_pot_with_liquid_is_held_too(void)
{
    th_t h;
    const safety_config_t cfg = cookware_config(SAFETY_COOKWARE_LIQUID);
    th_start(&h, &cfg, 0);
    cook_then_leave(&h);
    run_curve(&h, 600000, off_liquid); /* 10 min: still hot (about 115 degC) */
    TEST_ASSERT_TRUE(h.sm.state != SAFETY_WARNING && h.sm.state != SAFETY_SHUTDOWN);
    TEST_ASSERT_EQUAL(SAFETY_HEAT_COOLING, h.sm.heat_phase);
}

static void test_low_flame_is_not_mistaken_for_cooling(void)
{
    th_t h;
    const safety_config_t cfg = cookware_config(SAFETY_COOKWARE_LIGHT);
    th_start(&h, &cfg, 0);
    cook_then_leave(&h);
    /* Turned down to low: the temperature falls, but it settles at 90 degC. */
    run_curve(&h, 130000, low_flame);
    TH_EXPECT(&h, SAFETY_WARNING);
    TEST_ASSERT_TRUE(h.sm.heat_phase == SAFETY_HEAT_HOLDING || h.sm.heat_phase == SAFETY_HEAT_HEATING);
    TEST_ASSERT_TRUE(h.sm.settle_c > 70.0f);
}

/*
 * A profile slower than the pan can make a lowered flame look like cooling while its warm-up tail is still
 * relaxing. Within 1.5x of the pan the buzzer is on time; 2x to 8x slower it is late by up to about a minute
 * (measured: tools/diagnostics/cooling_delay.c), never missed.
 */
static void test_wrong_profile_delays_a_lowered_flame_at_most_a_minute(void)
{
    const safety_cookware_t slower[] = {SAFETY_COOKWARE_MIXED, SAFETY_COOKWARE_HEAVY, SAFETY_COOKWARE_LIQUID};
    for (unsigned i = 0; i < sizeof(slower) / sizeof(slower[0]); i++) { /* 2x, 3x and 8x the 5 min pan */
        th_t h;
        const safety_config_t cfg = cookware_config(slower[i]);
        th_start(&h, &cfg, 0);
        cook_then_leave(&h);
        run_curve(&h, cfg.warning_timeout_ms + 90000, low_flame);
        TEST_ASSERT_TRUE_MESSAGE(h.sm.state == SAFETY_WARNING, safety_cookware_name(slower[i]));
    }
}

static void test_boiling_plateau_counts_as_heat_going_in(void)
{
    th_t h;
    const safety_config_t cfg = cookware_config(SAFETY_COOKWARE_LIQUID);
    th_start(&h, &cfg, 0);
    th_cook(&h);
    curve_start(&h);
    run_curve(&h, 150000, boiling);
    h.in.presence = RB_FALSE;
    run_curve(&h, 130000, boiling);
    TH_EXPECT(&h, SAFETY_WARNING);
    TEST_ASSERT_EQUAL(SAFETY_HEAT_HOLDING, h.sm.heat_phase);
}

static void test_cooling_hold_ends_when_the_trend_flattens_hot(void)
{
    th_t h;
    const safety_config_t cfg = cookware_config(SAFETY_COOKWARE_LIGHT);
    th_start(&h, &cfg, 0);
    cook_then_leave(&h);
    run_curve(&h, 200000, off_light); /* held while cooling (about 88 degC at the end) */
    TEST_ASSERT_TRUE(h.sm.state != SAFETY_WARNING);
    /* The burner is turned on low: the pan rises a little and settles at 90 degC. */
    curve_start(&h);
    for (int i = 0; i < 200 && h.sm.state == SAFETY_UNATTENDED; i++) { /* the estimate needs about a minute to turn */
        run_curve(&h, 1000, low_from_88);
    }
    TH_EXPECT(&h, SAFETY_WARNING); /* the buzzer first, not a shutdown that was due long ago */
    TEST_ASSERT_EQUAL(SAFETY_BUZZER_WARNING, h.out.buzzer);
    run_curve(&h, 20000, low_from_88);
    TH_EXPECT(&h, SAFETY_WARNING); /* at least 30 s of warning after a hold */
    run_curve(&h, 20000, low_from_88);
    TH_EXPECT(&h, SAFETY_SHUTDOWN);
}

static void test_unknown_trend_counts_as_heat_going_in(void)
{
    th_t h;
    const safety_config_t cfg = cookware_config(SAFETY_COOKWARE_LIGHT);
    th_start(&h, &cfg, 0);
    cook_then_leave(&h);
    h.in.thermal_valid = false; /* the camera is gone: no trend, the last cooking state is kept */
    th_run(&h, cfg.warning_timeout_ms + 5000);
    TH_EXPECT(&h, SAFETY_WARNING);
    TEST_ASSERT_EQUAL(SAFETY_HEAT_CHECKING, h.sm.heat_phase);
}

static void test_trend_off_keeps_the_old_behaviour(void)
{
    th_t h;
    safety_config_t cfg = cookware_config(SAFETY_COOKWARE_LIGHT);
    cfg.cookware_tau_s = 0.0f; /* trend disabled */
    th_start(&h, &cfg, 0);
    cook_then_leave(&h);
    run_curve(&h, 200000, off_light); /* cooling, but nobody is looking at the trend */
    TH_EXPECT(&h, SAFETY_WARNING);
}

/* ---- a due timer waits while a return is being confirmed (bounded) ---- */

static void test_warning_waits_for_a_return_in_progress(void)
{
    th_t h;
    const safety_config_t cfg = return_filter_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    leave(&h);
    th_run(&h, cfg.warning_timeout_ms - safety_unattended_ms(&h.sm, h.now) - 1500);
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    h.in.presence = RB_TRUE; /* someone steps up just before the buzzer */
    th_run(&h, 3000);        /* the warning is due in the middle of this */
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    TEST_ASSERT_EQUAL(SAFETY_HOLD_RETURN, h.sm.hold);
    th_run(&h, 600); /* 3.6 s of presence: the return is confirmed */
    TH_EXPECT(&h, SAFETY_MONITORING);
}

static void test_warning_fires_when_the_return_does_not_materialise(void)
{
    th_t h;
    const safety_config_t cfg = return_filter_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    leave(&h);
    th_run(&h, cfg.warning_timeout_ms - safety_unattended_ms(&h.sm, h.now) - 500);
    h.in.presence = RB_TRUE;
    th_run(&h, 1000); /* due while held */
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    h.in.presence = RB_FALSE; /* it was a wave: the held warning comes at once */
    th_run(&h, 3 * TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_WARNING);
}

static void test_shutdown_waits_for_a_return_in_progress(void)
{
    th_t h;
    const safety_config_t cfg = return_filter_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    leave(&h);
    th_run(&h, cfg.shutdown_timeout_ms - safety_unattended_ms(&h.sm, h.now) - 1500);
    TH_EXPECT(&h, SAFETY_WARNING);
    h.in.presence = RB_TRUE;
    th_run(&h, 2000); /* shutdown due, return half confirmed */
    TH_EXPECT(&h, SAFETY_WARNING);
    TEST_ASSERT_EQUAL(SAFETY_HOLD_RETURN, h.sm.hold);
    th_run(&h, 1500);
    TH_EXPECT(&h, SAFETY_MONITORING); /* confirmed: no shutdown */
}

static void test_long_return_holds_the_warning_until_it_is_confirmed(void)
{
    th_t h;
    safety_config_t cfg = return_filter_config();
    cfg.presence_return_debounce_ms = 30000; /* a return that takes 30 s to confirm */
    th_start(&h, &cfg, 0);
    th_cook(&h);
    leave(&h);
    th_run(&h, cfg.warning_timeout_ms - safety_unattended_ms(&h.sm, h.now) - 500);
    h.in.presence = RB_TRUE;
    th_run(&h, 20000); /* the warning was due 19.5 s ago: held, the return is still being confirmed */
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    TEST_ASSERT_EQUAL(SAFETY_HOLD_RETURN, h.sm.hold);
    th_run(&h, 11000);
    TH_EXPECT(&h, SAFETY_MONITORING); /* confirmed: the warning never came */
}

static void test_sparse_blips_do_not_hold_a_due_warning(void)
{
    th_t h;
    const safety_config_t cfg = return_filter_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    leave(&h);
    th_run(&h, cfg.warning_timeout_ms - safety_unattended_ms(&h.sm, h.now) - 300);
    for (int i = 0; i < 3; i++) { /* a 200 ms blip every 1.5 s */
        blip(&h, 200, 1300);
    }
    TH_EXPECT(&h, SAFETY_WARNING);
}

/* ---- temperature tiers: the hotter the pan, the shorter the time ---- */

static safety_config_t tier_config(bool trend)
{
    safety_config_t cfg = th_default_config();
    cfg.cooling_policy = SAFETY_COOLING_RETURN_TO_IDLE;
    cfg.warning_timeout_ms = 180000;
    cfg.shutdown_timeout_ms = 300000;
    cfg.absence_debounce_ms = 1000;
    cfg.tier1_temp_c = 120.0f;
    cfg.tier1_warning_ms = 60000;
    cfg.tier1_shutdown_ms = 150000;
    cfg.tier2_temp_c = 200.0f;
    cfg.tier2_rate_c_per_min = 20.0f;
    cfg.tier2_rate_min_temp_c = 150.0f;
    cfg.tier2_warning_ms = 30000;
    cfg.tier2_shutdown_ms = 120000;
    cfg.cookware = SAFETY_COOKWARE_MIXED;
    cfg.cookware_tau_s = trend ? safety_cookware_tau_s(SAFETY_COOKWARE_MIXED) : 0.0f;
    return cfg;
}

static float const_temp_c;
static float constant_temp(float t) { (void)t; return const_temp_c; }

/* A pan at a steady temperature, the person leaves: seconds until the buzzer and until the shutdown. */
static void leave_and_time(float temp_c, bool trend, uint32_t *warning_s, uint32_t *shutdown_s)
{
    th_t h;
    const safety_config_t cfg = tier_config(trend);
    th_start(&h, &cfg, 0);
    const_temp_c = temp_c;
    th_cook(&h);
    curve_start(&h);
    run_curve(&h, 200000, constant_temp); /* a long steady cook: the trend knows it is steady */
    h.in.presence = RB_FALSE;
    const uint32_t t0 = h.now;
    *warning_s = *shutdown_s = 0;
    while (h.now - t0 < 400000 && h.sm.state != SAFETY_SHUTDOWN) {
        run_curve(&h, 1000, constant_temp);
        if (*warning_s == 0 && h.sm.state == SAFETY_WARNING) *warning_s = (h.now - t0) / 1000;
    }
    *shutdown_s = h.sm.state == SAFETY_SHUTDOWN ? (h.now - t0) / 1000 : 0;
}

static void test_a_simmering_pot_gets_the_base_times(void)
{
    uint32_t w, s;
    leave_and_time(95.0f, true, &w, &s);
    TEST_ASSERT_UINT32_WITHIN(3, 180, w);
    TEST_ASSERT_UINT32_WITHIN(3, 300, s);
}

static void test_a_hot_pan_gets_shorter_times(void)
{
    uint32_t w, s;
    leave_and_time(150.0f, true, &w, &s);
    TEST_ASSERT_UINT32_WITHIN(3, 60, w);
    TEST_ASSERT_UINT32_WITHIN(3, 150, s);
}

static void test_oil_near_its_smoke_point_gets_the_shortest_times(void)
{
    uint32_t w, s;
    leave_and_time(220.0f, true, &w, &s);
    TEST_ASSERT_UINT32_WITHIN(3, 30, w);
    TEST_ASSERT_UINT32_WITHIN(3, 120, s);
}

static void test_tiers_never_lengthen_the_base_times(void)
{
    th_t h;
    safety_config_t cfg = tier_config(false);
    cfg.warning_timeout_ms = 10000; /* the base times are already shorter than the tiers' */
    cfg.shutdown_timeout_ms = 20000;
    th_start(&h, &cfg, 0);
    const_temp_c = 220.0f;
    th_cook(&h);
    curve_start(&h);
    run_curve(&h, 20000, constant_temp);
    h.in.presence = RB_FALSE;
    run_curve(&h, 12000, constant_temp);
    TH_EXPECT(&h, SAFETY_WARNING);
    TEST_ASSERT_EQUAL_UINT32(10000, h.sm.eff_warning_ms);
}

static void test_a_pan_that_heats_up_while_nobody_is_there_shortens_the_time(void)
{
    th_t h;
    const safety_config_t cfg = tier_config(false);
    th_start(&h, &cfg, 0);
    const_temp_c = 95.0f;
    th_cook(&h);
    curve_start(&h);
    run_curve(&h, 60000, constant_temp);
    h.in.presence = RB_FALSE;
    run_curve(&h, 100000, constant_temp); /* 100 s away at a simmer: base times, nothing yet */
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    const_temp_c = 230.0f; /* the pot boiled dry and the oil/pan is heading up */
    run_curve(&h, 3000, constant_temp);
    TH_EXPECT(&h, SAFETY_UNATTENDED); /* not before the temperature has held for a few seconds */
    run_curve(&h, 3000, constant_temp);
    TH_EXPECT(&h, SAFETY_WARNING); /* 100 s away is long past the 30 s limit of that tier */
    TEST_ASSERT_EQUAL(SAFETY_TIER_CRITICAL, h.sm.tier);
    run_curve(&h, 8000, constant_temp);
    TH_EXPECT(&h, SAFETY_WARNING); /* the buzzer still sounds before the shutdown, which is due at 120 s */
    run_curve(&h, 16000, constant_temp);
    TH_EXPECT(&h, SAFETY_SHUTDOWN);
}

static float spike_curve(float t) { return t > 50.0f && t < 52.0f ? 250.0f : 95.0f; }

static void test_a_two_second_spike_does_not_shorten_the_time(void)
{
    th_t h;
    const safety_config_t cfg = tier_config(false);
    th_start(&h, &cfg, 0);
    th_cook(&h);
    curve_start(&h);
    run_curve(&h, 80000, spike_curve);
    TEST_ASSERT_EQUAL(SAFETY_TIER_NORMAL, h.sm.tier);
}

static float ramp_30_per_min(float t) { return 130.0f + 0.5f * t; }

static void test_rising_fast_toward_the_smoke_point_counts_as_critical(void)
{
    th_t h;
    const safety_config_t cfg = tier_config(true);
    th_start(&h, &cfg, 0);
    th_cook(&h);
    curve_start(&h);
    run_curve(&h, 110000, ramp_30_per_min); /* 185 degC now, rising 30 degC/min: below 200 but heading there */
    TEST_ASSERT_TRUE(h.in.cooking_temp_c < 200.0f);
    TEST_ASSERT_EQUAL(SAFETY_TIER_CRITICAL, h.sm.tier);
}

static void test_a_tier_gives_way_only_well_below_its_temperature(void)
{
    th_t h;
    const safety_config_t cfg = tier_config(false);
    th_start(&h, &cfg, 0);
    const_temp_c = 220.0f;
    th_cook(&h);
    curve_start(&h);
    run_curve(&h, 20000, constant_temp);
    TEST_ASSERT_EQUAL(SAFETY_TIER_CRITICAL, h.sm.tier);
    const_temp_c = 195.0f; /* only 5 degC under the threshold: stays */
    run_curve(&h, 60000, constant_temp);
    TEST_ASSERT_EQUAL(SAFETY_TIER_CRITICAL, h.sm.tier);
    const_temp_c = 150.0f; /* well below: after a while it drops to the next tier */
    run_curve(&h, 15000, constant_temp);
    TEST_ASSERT_EQUAL(SAFETY_TIER_CRITICAL, h.sm.tier);
    run_curve(&h, 10000, constant_temp);
    TEST_ASSERT_EQUAL(SAFETY_TIER_ELEVATED, h.sm.tier);
}

static void test_a_lost_camera_keeps_the_tier(void)
{
    th_t h;
    const safety_config_t cfg = tier_config(false);
    th_start(&h, &cfg, 0);
    const_temp_c = 220.0f;
    th_cook(&h);
    curve_start(&h);
    run_curve(&h, 20000, constant_temp);
    h.in.thermal_valid = false;
    h.in.presence = RB_FALSE;
    th_run(&h, 40000);
    TH_EXPECT(&h, SAFETY_WARNING); /* hot oil does not become safe because the camera stopped */
}

static void test_tier_config_is_checked(void)
{
    safety_config_t cfg = tier_config(false);
    TEST_ASSERT_TRUE(safety_config_valid(&cfg));
    cfg.tier2_shutdown_ms = cfg.tier2_warning_ms; /* shutdown not after the warning */
    TEST_ASSERT_FALSE(safety_config_valid(&cfg));
    cfg = tier_config(false);
    cfg.tier2_temp_c = 100.0f; /* CRITICAL below ELEVATED */
    TEST_ASSERT_FALSE(safety_config_valid(&cfg));
    cfg = tier_config(false);
    cfg.tier1_temp_c = 0.0f; /* tiers off */
    cfg.tier2_temp_c = 0.0f;
    TEST_ASSERT_TRUE(safety_config_valid(&cfg));
}

/* ---- fine tuning from the config (rb_params.def): 0 = the built-in value ---- */

static void test_built_in_tuning_is_filled_in(void)
{
    safety_config_t cfg = th_default_config();
    safety_config_fill_defaults(&cfg);
    TEST_ASSERT_EQUAL_UINT32(4000, cfg.tier_up_persist_ms);
    TEST_ASSERT_EQUAL_UINT32(20000, cfg.tier_down_persist_ms);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, cfg.tier_hyst_c);
    TEST_ASSERT_EQUAL_UINT32(120000, cfg.trend_window_ms);
    TEST_ASSERT_EQUAL_UINT16(12, cfg.trend_min_samples);
    TEST_ASSERT_EQUAL_FLOAT(4.0f, cfg.cooling_hold_taus);
    TEST_ASSERT_EQUAL_UINT32(30000, cfg.warning_min_after_hold_ms);
    TEST_ASSERT_TRUE(safety_config_valid(&cfg));
    cfg.tier_hyst_c = 3.0f; /* a value already set stays */
    safety_config_fill_defaults(&cfg);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, cfg.tier_hyst_c);
}

static void test_tuned_tier_timing_is_used(void)
{
    th_t h;
    safety_config_t cfg = tier_config(false);
    cfg.tier_up_persist_ms = 1000; /* a 2 s spike now counts ... */
    cfg.tier_hyst_c = 2.0f;        /* ... and 5 degC under the threshold is enough to give way */
    cfg.tier_down_persist_ms = 5000;
    th_start(&h, &cfg, 0);
    th_cook(&h);
    curve_start(&h);
    run_curve(&h, 52000, spike_curve);
    TEST_ASSERT_EQUAL(SAFETY_TIER_CRITICAL, h.sm.tier);
    const_temp_c = 195.0f;
    run_curve(&h, 4000, constant_temp);
    TEST_ASSERT_EQUAL(SAFETY_TIER_CRITICAL, h.sm.tier);
    run_curve(&h, 2000, constant_temp);
    TEST_ASSERT_EQUAL(SAFETY_TIER_ELEVATED, h.sm.tier);
}

static void test_tuning_config_is_checked(void)
{
    safety_config_t cfg = th_default_config();
    cfg.trend_min_samples = SAFETY_TREND_MAX + 1; /* more samples than the buffer holds */
    TEST_ASSERT_FALSE(safety_config_valid(&cfg));
    cfg = th_default_config();
    cfg.trend_window_ms = SAFETY_TREND_MAX * 2000u + 2000u;
    TEST_ASSERT_FALSE(safety_config_valid(&cfg));
    cfg = th_default_config();
    cfg.settle_margin_c = -1.0f;
    TEST_ASSERT_FALSE(safety_config_valid(&cfg));
}

/* ---- pause: someone passing stops the clock, only a stay of 5 s cancels ---- */

static safety_config_t pause_config(void)
{
    safety_config_t cfg = th_default_config();
    cfg.absence_debounce_ms = 1000;
    cfg.presence_return_debounce_ms = 5000;
    cfg.presence_return_gap_ms = 1000;
    cfg.warning_timeout_ms = 180000;
    cfg.shutdown_timeout_ms = 300000;
    cfg.pause_budget_ms = 30000;
    cfg.pause_budget_pct = 25;
    return cfg;
}

static void leave_for(th_t *h, uint32_t ms)
{
    th_cook(h);
    h->in.presence = RB_FALSE;
    th_run(h, 1000 + TH_STEP_MS);
    TH_EXPECT(h, SAFETY_UNATTENDED);
    th_run(h, ms);
}

static void test_a_short_pass_stops_the_clock_and_resets_nothing(void)
{
    th_t h;
    const safety_config_t cfg = pause_config();
    th_start(&h, &cfg, 0);
    leave_for(&h, 40000);
    const uint32_t before = safety_unattended_ms(&h.sm, h.now);
    h.in.presence = RB_TRUE;
    th_run(&h, 3000); /* someone passes, 3 s */
    TEST_ASSERT_TRUE(h.sm.paused);
    TEST_ASSERT_UINT32_WITHIN(2 * TH_STEP_MS, before, safety_unattended_ms(&h.sm, h.now)); /* the clock stood still */
    h.in.presence = RB_FALSE;
    th_run(&h, 10000);
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    TEST_ASSERT_FALSE(h.sm.paused);
    TEST_ASSERT_UINT32_WITHIN(2 * TH_STEP_MS, before + 10000, safety_unattended_ms(&h.sm, h.now)); /* and runs on */
}

static void test_five_seconds_of_presence_cancel_the_timeline(void)
{
    th_t h;
    const safety_config_t cfg = pause_config();
    th_start(&h, &cfg, 0);
    leave_for(&h, 40000);
    h.in.presence = RB_TRUE;
    th_run(&h, 4500);
    TH_EXPECT(&h, SAFETY_UNATTENDED); /* still only a pause */
    th_run(&h, 1000);
    TH_EXPECT(&h, SAFETY_MONITORING); /* 5 s: someone is at the stove, the timeline is cancelled */
    TEST_ASSERT_EQUAL_UINT32(0, safety_unattended_ms(&h.sm, h.now));
    TEST_ASSERT_EQUAL_UINT32(0, h.sm.pause_used_ms);
}

static void test_the_pause_budget_limits_the_delay(void)
{
    th_t h;
    safety_config_t cfg = pause_config();
    cfg.pause_budget_ms = 10000;
    th_start(&h, &cfg, 0);
    leave_for(&h, 20000);
    const uint32_t before = safety_unattended_ms(&h.sm, h.now);
    for (int i = 0; i < 20; i++) { /* a busy aisle: 3 s of someone, 2 s of nobody, again and again */
        h.in.presence = RB_TRUE;
        th_run(&h, 3000);
        h.in.presence = RB_FALSE;
        th_run(&h, 2000);
    }
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    TEST_ASSERT_EQUAL_UINT32(10000, h.sm.pause_used_ms);
    /* 100 s went by, of which at most 10 s did not count. */
    TEST_ASSERT_UINT32_WITHIN(3 * TH_STEP_MS, before + 100000 - 10000, safety_unattended_ms(&h.sm, h.now));
}

static void test_the_budget_is_a_share_of_the_shutdown_time(void)
{
    th_t h;
    safety_config_t cfg = pause_config();
    cfg.pause_budget_ms = 600000;
    cfg.pause_budget_pct = 25;
    cfg.shutdown_timeout_ms = 120000; /* 25 % of this is 30 s */
    cfg.warning_timeout_ms = 60000;
    th_start(&h, &cfg, 0);
    leave_for(&h, 10000);
    TEST_ASSERT_EQUAL_UINT32(30000, safety_pause_budget_ms(&h.sm));
}

static void test_no_budget_means_no_pause(void)
{
    th_t h;
    safety_config_t cfg = pause_config();
    cfg.pause_budget_ms = 0;
    th_start(&h, &cfg, 0);
    leave_for(&h, 40000);
    const uint32_t before = safety_unattended_ms(&h.sm, h.now);
    h.in.presence = RB_TRUE;
    th_run(&h, 3000);
    TEST_ASSERT_FALSE(h.sm.paused);
    TEST_ASSERT_UINT32_WITHIN(2 * TH_STEP_MS, before + 3000, safety_unattended_ms(&h.sm, h.now));
}

static void test_the_pause_postpones_the_shutdown_by_what_it_paused(void)
{
    th_t h;
    const safety_config_t cfg = pause_config();
    th_start(&h, &cfg, 0);
    leave_for(&h, 181000);
    TH_EXPECT(&h, SAFETY_WARNING);
    const uint32_t start_before = h.sm.unattended_start_ms;
    h.in.presence = RB_TRUE;
    th_run(&h, 4000); /* passes during the warning */
    h.in.presence = RB_FALSE;
    th_run(&h, 2 * TH_STEP_MS);
    TEST_ASSERT_UINT32_WITHIN(2 * TH_STEP_MS, 4000, h.sm.unattended_start_ms - start_before); /* moved by what it paused */
    const uint32_t left = cfg.shutdown_timeout_ms - safety_unattended_ms(&h.sm, h.now);
    th_run(&h, left - 5 * TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_WARNING);
    th_run(&h, 10 * TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_SHUTDOWN);
}

void run_safety_tests(void)
{
    RUN_TEST(test_boot_selftest_pass_to_idle);
    RUN_TEST(test_boot_selftest_fail_or_timeout_to_fault);
    RUN_TEST(test_idle_to_monitoring);
    RUN_TEST(test_monitoring_to_unattended_after_debounce);
    RUN_TEST(test_unattended_to_monitoring_person_returns);
    RUN_TEST(test_unattended_to_warning);
    RUN_TEST(test_warning_to_shutdown_total_time);
    RUN_TEST(test_shutdown_timing_after_warning_mode);
    RUN_TEST(test_person_returns_during_warning);
    RUN_TEST(test_temperature_drops_during_unattended);
    RUN_TEST(test_presence_unknown_no_heat_flag_only);
    RUN_TEST(test_presence_unknown_while_cooking_runs_timeline);
    RUN_TEST(test_presence_unknown_then_confirmed_return_cancels);
    RUN_TEST(test_presence_unknown_keeps_running_timeline);
    RUN_TEST(test_thermal_lost_flag_only);
    RUN_TEST(test_thermal_lost_keeps_running_timeline);
    RUN_TEST(test_thermal_lost_with_person_present_stays_monitoring);
    RUN_TEST(test_fault_only_for_controller_faults);
    RUN_TEST(test_safety_fault_input);
    RUN_TEST(test_reset_restarts_timing);
    RUN_TEST(test_heat_detected_after_person_left);
    RUN_TEST(test_rate_threshold_optional);
    RUN_TEST(test_timing_hook_can_only_shorten);
    RUN_TEST(test_monotonic_wraparound);
    RUN_TEST(test_config_validation);
    RUN_TEST(test_wave_keeps_unattended_timer);
    RUN_TEST(test_wave_during_warning_keeps_shutdown);
    RUN_TEST(test_return_survives_short_flicker);
    RUN_TEST(test_long_gap_restarts_return_count);
    RUN_TEST(test_return_needs_presence_when_count_completes);
    RUN_TEST(test_presence_blips_do_not_block_absence);
    RUN_TEST(test_blip_inside_debounce_keeps_absent_since);
    RUN_TEST(test_real_return_after_noise_cancels_timer);
    RUN_TEST(test_cooling_pan_holds_the_timers_then_cancels);
    RUN_TEST(test_cooling_hold_is_reported);
    RUN_TEST(test_slow_cooling_pot_with_liquid_is_held_too);
    RUN_TEST(test_low_flame_is_not_mistaken_for_cooling);
    RUN_TEST(test_wrong_profile_delays_a_lowered_flame_at_most_a_minute);
    RUN_TEST(test_boiling_plateau_counts_as_heat_going_in);
    RUN_TEST(test_cooling_hold_ends_when_the_trend_flattens_hot);
    RUN_TEST(test_unknown_trend_counts_as_heat_going_in);
    RUN_TEST(test_trend_off_keeps_the_old_behaviour);
    RUN_TEST(test_warning_waits_for_a_return_in_progress);
    RUN_TEST(test_warning_fires_when_the_return_does_not_materialise);
    RUN_TEST(test_shutdown_waits_for_a_return_in_progress);
    RUN_TEST(test_long_return_holds_the_warning_until_it_is_confirmed);
    RUN_TEST(test_sparse_blips_do_not_hold_a_due_warning);
    RUN_TEST(test_a_simmering_pot_gets_the_base_times);
    RUN_TEST(test_a_hot_pan_gets_shorter_times);
    RUN_TEST(test_oil_near_its_smoke_point_gets_the_shortest_times);
    RUN_TEST(test_tiers_never_lengthen_the_base_times);
    RUN_TEST(test_a_pan_that_heats_up_while_nobody_is_there_shortens_the_time);
    RUN_TEST(test_a_two_second_spike_does_not_shorten_the_time);
    RUN_TEST(test_rising_fast_toward_the_smoke_point_counts_as_critical);
    RUN_TEST(test_a_tier_gives_way_only_well_below_its_temperature);
    RUN_TEST(test_a_lost_camera_keeps_the_tier);
    RUN_TEST(test_tier_config_is_checked);
    RUN_TEST(test_built_in_tuning_is_filled_in);
    RUN_TEST(test_tuned_tier_timing_is_used);
    RUN_TEST(test_tuning_config_is_checked);
    RUN_TEST(test_a_short_pass_stops_the_clock_and_resets_nothing);
    RUN_TEST(test_five_seconds_of_presence_cancel_the_timeline);
    RUN_TEST(test_the_pause_budget_limits_the_delay);
    RUN_TEST(test_the_budget_is_a_share_of_the_shutdown_time);
    RUN_TEST(test_no_budget_means_no_pause);
    RUN_TEST(test_the_pause_postpones_the_shutdown_by_what_it_paused);
}
