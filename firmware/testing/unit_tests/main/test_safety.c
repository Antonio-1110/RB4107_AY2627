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
        .hot_region_temp_c = 25.0f,
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
    h->in.hot_region_temp_c = 120.0f;
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
    h.in = (safety_inputs_t){.presence = RB_TRUE, .thermal_valid = true, .hot_region_temp_c = 25, .self_test = SAFETY_SELFTEST_FAIL};
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
    h.in.hot_region_temp_c = 49.9f;
    th_run(&h, 1000);
    TH_EXPECT(&h, SAFETY_IDLE);
    h.in.hot_region_temp_c = 50.0f;
    th_run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_MONITORING);
    /* Hysteresis: stays until below heat_off. */
    h.in.hot_region_temp_c = 45.0f;
    th_run(&h, 1000);
    TH_EXPECT(&h, SAFETY_MONITORING);
    h.in.hot_region_temp_c = 39.0f;
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
    th_run(&h, 2 * TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_SHUTDOWN);
    TEST_ASSERT_TRUE(h.out.shutdown);
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
    h.in.hot_region_temp_c = 30.0f;
    th_run(&h, cfg.shutdown_timeout_ms);
    TH_EXPECT(&h, SAFETY_SHUTDOWN);

    cfg.cooling_policy = SAFETY_COOLING_RETURN_TO_IDLE;
    th_start(&h, &cfg, 0);
    th_cook(&h);
    leave(&h);
    h.in.hot_region_temp_c = 30.0f;
    th_run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_IDLE);
    TEST_ASSERT_EQUAL_UINT32(0, safety_unattended_ms(&h.sm, h.now));
}

static void test_c4002_unavailable_is_not_absence(void)
{
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    h.in.presence = RB_UNKNOWN;
    th_run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_FAULT);
    TEST_ASSERT_EQUAL(SAFETY_BUZZER_FAULT, h.out.buzzer);
    TEST_ASSERT_FALSE(h.out.shutdown);
    /* UNKNOWN never starts the unattended timer... */
    th_run(&h, cfg.warning_timeout_ms);
    TH_EXPECT(&h, SAFETY_FAULT);
    TEST_ASSERT_FALSE(h.sm.unattended_active);
    /* ...but a fault that persists shuts down. */
    th_run(&h, cfg.fault_shutdown_timeout_ms - cfg.warning_timeout_ms);
    TH_EXPECT(&h, SAFETY_SHUTDOWN);
}

static void test_c4002_recovers(void)
{
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    h.in.presence = RB_UNKNOWN;
    th_run(&h, 5000);
    TH_EXPECT(&h, SAFETY_FAULT);
    h.in.presence = RB_TRUE;
    th_run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_MONITORING);
}

static void test_sensor_loss_never_delays_shutdown(void)
{
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    h.in.presence = RB_FALSE;
    th_run(&h, 50000); /* 50 s unattended */
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    h.in.presence = RB_UNKNOWN; /* C4002 dies */
    th_run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_FAULT);
    th_run(&h, cfg.shutdown_timeout_ms - 50000 + TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_SHUTDOWN); /* still at 90 s total, not 50 + 90 */
}

static void test_fault_clears_with_person_absent_keeps_timeline(void)
{
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    h.in.presence = RB_FALSE;
    th_run(&h, 30000);
    h.in.thermal_valid = false; /* MLX dropout for 10 s */
    th_run(&h, 10000);
    TH_EXPECT(&h, SAFETY_FAULT);
    h.in.thermal_valid = true;
    th_run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    TEST_ASSERT_UINT32_WITHIN(500, 40000, safety_unattended_ms(&h.sm, h.now));
}

static void test_mlx_unavailable(void)
{
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    h.in.thermal_valid = false; /* even in IDLE: we can't tell whether the hob is on */
    th_run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_FAULT);
    h.in.thermal_valid = true;
    th_run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_IDLE);
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
    h.in.hot_region_temp_c = 120.0f;
    th_run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_UNATTENDED); /* cooking with nobody there */
    TEST_ASSERT_TRUE(safety_unattended_ms(&h.sm, h.now) <= TH_STEP_MS); /* counts from now, not 10 min ago */
}

static void test_rate_threshold_optional(void)
{
    th_t h;
    safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    h.in.hot_region_temp_c = 45.0f;
    h.in.temp_rate_c_per_min = 20.0f;
    th_run(&h, 1000);
    TH_EXPECT(&h, SAFETY_IDLE); /* rate trigger off by default */
    cfg.heat_on_rate_c_per_min = 10.0f;
    th_start(&h, &cfg, 0);
    h.in.hot_region_temp_c = 45.0f;
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
    RUN_TEST(test_c4002_unavailable_is_not_absence);
    RUN_TEST(test_c4002_recovers);
    RUN_TEST(test_sensor_loss_never_delays_shutdown);
    RUN_TEST(test_fault_clears_with_person_absent_keeps_timeline);
    RUN_TEST(test_mlx_unavailable);
    RUN_TEST(test_safety_fault_input);
    RUN_TEST(test_reset_restarts_timing);
    RUN_TEST(test_heat_detected_after_person_left);
    RUN_TEST(test_rate_threshold_optional);
    RUN_TEST(test_timing_hook_can_only_shorten);
    RUN_TEST(test_monotonic_wraparound);
    RUN_TEST(test_config_validation);
}
