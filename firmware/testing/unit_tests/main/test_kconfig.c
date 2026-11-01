/*
 * The menuconfig safety settings (what the controller firmware actually runs with) are
 * consistent and give the documented behaviour through the real state machine.
 * (TODO section 10.)
 */
#include <math.h>
#include "rb_safety_config.h"
#include "sdkconfig.h"
#include "test_harness.h"
#include "unity.h"

static void test_kconfig_safety_config_is_valid(void)
{
    const safety_config_t cfg = rb_safety_config_from_kconfig();
    TEST_ASSERT_TRUE(safety_config_valid(&cfg));
    TEST_ASSERT_EQUAL_UINT32(CONFIG_RB_SAFETY_WARNING_TIMEOUT_S * 1000u, cfg.warning_timeout_ms);
    TEST_ASSERT_EQUAL_UINT32(CONFIG_RB_SAFETY_SHUTDOWN_TIMEOUT_S * 1000u, cfg.shutdown_timeout_ms);
}

static void test_kconfig_warning_and_shutdown_timing(void)
{
    const safety_config_t cfg = rb_safety_config_from_kconfig();
    const uint32_t shutdown_after_leaving = cfg.shutdown_timing == SAFETY_SHUTDOWN_AFTER_UNATTENDED_START
                                                ? cfg.shutdown_timeout_ms
                                                : cfg.warning_timeout_ms + cfg.shutdown_timeout_ms;
    th_t h;
    th_start(&h, &cfg, 0);
    h.in.cooking_temp_c = cfg.heat_on_temp_c + 20.0f;
    th_run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_MONITORING);

    h.in.presence = RB_FALSE;
    th_run(&h, cfg.warning_timeout_ms - TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    th_run(&h, 2 * TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_WARNING);
    th_run(&h, shutdown_after_leaving - cfg.warning_timeout_ms - 2 * TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_WARNING);
    th_run(&h, 3 * TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_SHUTDOWN);
    TEST_ASSERT_TRUE(h.out.shutdown);
}

/*
 * The defaults must do the right thing for the situation, not just run. After a long cook the person leaves
 * and the burner is either switched off (the pan only cools down) or kept on (possibly turned down). The
 * limits depend on how hot the pan is: a simmer gets the base times, a hot pan or hot oil much shorter ones.
 */
typedef struct {
    int warning_s, shutdown_s, idle_s; /* -1: did not happen within 40 min */
} outcome_t;

/* The pan sits at start_c, the person leaves, and the pan then heads for end_c with a 10 min time constant. */
static outcome_t leave_and_watch(float start_c, float end_c)
{
    const safety_config_t cfg = rb_safety_config_from_kconfig();
    th_t h;
    th_start(&h, &cfg, 0);
    h.in.cooking_temp_c = start_c;
    th_run(&h, 150000);
    TH_EXPECT(&h, SAFETY_MONITORING);
    h.in.presence = RB_FALSE;
    const uint32_t t0 = h.now;
    outcome_t o = {-1, -1, -1};
    while (h.now - t0 < 2400000 && o.shutdown_s < 0 && o.idle_s < 0) {
        const float t = (float)(h.now - t0) / 1000.0f;
        h.in.cooking_temp_c = end_c + (start_c - end_c) * expf(-t / 600.0f);
        th_run(&h, TH_STEP_MS);
        if (o.warning_s < 0 && h.sm.state == SAFETY_WARNING) o.warning_s = (int)((h.now - t0) / 1000);
        if (h.sm.state == SAFETY_SHUTDOWN) o.shutdown_s = (int)((h.now - t0) / 1000);
        if (h.sm.state == SAFETY_IDLE) o.idle_s = (int)((h.now - t0) / 1000);
    }
    return o;
}

static void test_kconfig_a_simmer_gets_the_base_times(void)
{
    const safety_config_t cfg = rb_safety_config_from_kconfig();
    const outcome_t o = leave_and_watch(95.0f, 95.0f); /* burner on, steady */
    TEST_ASSERT_UINT32_WITHIN(5, cfg.warning_timeout_ms / 1000, o.warning_s);
    TEST_ASSERT_UINT32_WITHIN(5, cfg.shutdown_timeout_ms / 1000, o.shutdown_s);
}

static void test_kconfig_a_simmer_with_the_burner_off_is_left_alone(void)
{
    const outcome_t o = leave_and_watch(95.0f, 25.0f);
    TEST_ASSERT_EQUAL(-1, o.warning_s); /* no buzzer, no shutdown: it just cools down and the timeline ends */
    TEST_ASSERT_EQUAL(-1, o.shutdown_s);
    TEST_ASSERT_TRUE(o.idle_s > 0);
}

static void test_kconfig_a_hot_pan_on_low_gets_the_shorter_times(void)
{
    const outcome_t o = leave_and_watch(150.0f, 130.0f); /* heat still going in: tier 3 (150 degC and up) */
    TEST_ASSERT_UINT32_WITHIN(5, CONFIG_RB_SAFETY_TIER_3_WARNING_S, o.warning_s);
    TEST_ASSERT_UINT32_WITHIN(5, CONFIG_RB_SAFETY_TIER_3_SHUTDOWN_S, o.shutdown_s);
}

static void test_kconfig_hot_oil_with_the_burner_on_gets_shorter_times(void)
{
    const outcome_t o = leave_and_watch(220.0f, 210.0f); /* tier 4 */
    TEST_ASSERT_UINT32_WITHIN(5, CONFIG_RB_SAFETY_TIER_4_WARNING_S, o.warning_s);
    TEST_ASSERT_UINT32_WITHIN(5, CONFIG_RB_SAFETY_TIER_4_SHUTDOWN_S, o.shutdown_s);
}

static void test_kconfig_oil_past_its_smoke_point_gets_the_shortest_times(void)
{
    const outcome_t o = leave_and_watch(270.0f, 260.0f); /* tier 5 */
    TEST_ASSERT_UINT32_WITHIN(5, CONFIG_RB_SAFETY_TIER_5_WARNING_S, o.warning_s);
    TEST_ASSERT_UINT32_WITHIN(5, CONFIG_RB_SAFETY_TIER_5_SHUTDOWN_S, o.shutdown_s);
}

static void test_kconfig_hot_oil_turned_down_is_still_shut_down(void)
{
    const outcome_t o = leave_and_watch(220.0f, 120.0f); /* falling, but settling hot: heat still going in */
    /* The buzzer waits while the pan falls (falling hold, at most 90 s), then the shutdown follows. */
    TEST_ASSERT_TRUE(o.warning_s >= CONFIG_RB_SAFETY_TIER_4_WARNING_S);
    TEST_ASSERT_TRUE(o.warning_s <= CONFIG_RB_SAFETY_TIER_4_WARNING_S + 95);
    TEST_ASSERT_TRUE(o.shutdown_s > o.warning_s);
    TEST_ASSERT_EQUAL(-1, o.idle_s);
}

static void test_kconfig_hot_oil_with_the_burner_off_is_left_alone(void)
{
    /* Switched off at tier 4 or 5: the pan falls at once, so the short buzzer waits (falling hold) until the
     * trend sees it is only cooling (cooling hold). No buzzer, no shutdown; the timeline ends when it is cold. */
    const float hot[] = {220.0f, 270.0f};
    for (unsigned i = 0; i < sizeof(hot) / sizeof(hot[0]); i++) {
        const outcome_t o = leave_and_watch(hot[i], 25.0f);
        TEST_ASSERT_EQUAL(-1, o.warning_s);
        TEST_ASSERT_EQUAL(-1, o.shutdown_s);
        TEST_ASSERT_TRUE(o.idle_s > 0);
    }
}

/* A hand waved at the radar (about 2 s of presence) must not cancel the unattended timers; it only stops the clock for those 2 s. */
static void test_kconfig_wave_keeps_unattended(void)
{
    const safety_config_t cfg = rb_safety_config_from_kconfig();
    th_t h;
    th_start(&h, &cfg, 0);
    h.in.cooking_temp_c = cfg.heat_on_temp_c + 20.0f;
    th_run(&h, TH_STEP_MS);
    h.in.presence = RB_FALSE;
    th_run(&h, cfg.absence_debounce_ms + 10000);
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    const uint32_t before = safety_unattended_ms(&h.sm, h.now);
    h.in.presence = RB_TRUE;
    th_run(&h, 2000);
    h.in.presence = RB_FALSE;
    th_run(&h, 2000);
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    TEST_ASSERT_UINT32_WITHIN(3 * TH_STEP_MS, before + 2000, safety_unattended_ms(&h.sm, h.now)); /* 2 s paused, 2 s counted */
}

void run_kconfig_tests(void)
{
    RUN_TEST(test_kconfig_safety_config_is_valid);
    RUN_TEST(test_kconfig_warning_and_shutdown_timing);
    RUN_TEST(test_kconfig_wave_keeps_unattended);
    RUN_TEST(test_kconfig_a_simmer_gets_the_base_times);
    RUN_TEST(test_kconfig_a_simmer_with_the_burner_off_is_left_alone);
    RUN_TEST(test_kconfig_a_hot_pan_on_low_gets_the_shorter_times);
    RUN_TEST(test_kconfig_hot_oil_with_the_burner_on_gets_shorter_times);
    RUN_TEST(test_kconfig_oil_past_its_smoke_point_gets_the_shortest_times);
    RUN_TEST(test_kconfig_hot_oil_turned_down_is_still_shut_down);
    RUN_TEST(test_kconfig_hot_oil_with_the_burner_off_is_left_alone);
}
