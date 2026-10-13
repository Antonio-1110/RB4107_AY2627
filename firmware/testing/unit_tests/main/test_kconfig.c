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
 * The defaults must do the right thing for the situation, not just run: after a
 * long cook the person leaves and the burner is either switched off (the pan
 * only cools: no warning, no shutdown, and the timeline ends when it is cold) or
 * turned down to low (heat still goes in: the buzzer at the warning time and the
 * shutdown at the shutdown time).
 */
static void leave_after_a_long_cook(th_t *h, const safety_config_t *cfg)
{
    th_start(h, cfg, 0);
    h->in.cooking_temp_c = 150.0f;
    th_run(h, 150000);
    TH_EXPECT(h, SAFETY_MONITORING);
    h->in.presence = RB_FALSE;
}

static void test_kconfig_burner_off_pan_cooling_is_not_warned(void)
{
    const safety_config_t cfg = rb_safety_config_from_kconfig();
    th_t h;
    leave_after_a_long_cook(&h, &cfg);
    const uint32_t t0 = h.now;
    for (uint32_t t = 0; t < 400000; t += TH_STEP_MS) { /* past the warning time and the shutdown time */
        h.in.cooking_temp_c = 25.0f + 125.0f * expf(-(float)t / 1000.0f / 600.0f); /* a pan with the default 10 min profile */
        th_run(&h, TH_STEP_MS);
        TEST_ASSERT_TRUE(h.sm.state != SAFETY_WARNING && h.sm.state != SAFETY_SHUTDOWN);
    }
    TEST_ASSERT_TRUE(h.now - t0 > cfg.shutdown_timeout_ms);
    /* Still cooling after 400 s (about 85 degC): the hold ends when it is below the cooking temperature. */
    for (uint32_t t = 400000; t < 1500000; t += TH_STEP_MS) {
        h.in.cooking_temp_c = 25.0f + 125.0f * expf(-(float)t / 1000.0f / 600.0f);
        th_run(&h, TH_STEP_MS);
        TEST_ASSERT_TRUE(h.sm.state != SAFETY_WARNING && h.sm.state != SAFETY_SHUTDOWN);
    }
    TH_EXPECT(&h, SAFETY_IDLE);
}

static void test_kconfig_burner_on_low_is_warned_and_shut_down(void)
{
    const safety_config_t cfg = rb_safety_config_from_kconfig();
    th_t h;
    leave_after_a_long_cook(&h, &cfg);
    const uint32_t t0 = h.now;
    uint32_t warning_at = 0, shutdown_at = 0;
    for (uint32_t t = 0; t < cfg.shutdown_timeout_ms + 60000; t += TH_STEP_MS) {
        h.in.cooking_temp_c = 90.0f + 60.0f * expf(-(float)t / 1000.0f / 600.0f); /* settles at 90 degC */
        th_run(&h, TH_STEP_MS);
        if (warning_at == 0 && h.sm.state == SAFETY_WARNING) warning_at = h.now - t0;
        if (shutdown_at == 0 && h.sm.state == SAFETY_SHUTDOWN) shutdown_at = h.now - t0;
    }
    /* On time, within a few seconds: the trend only holds things back for a cooling pan. */
    TEST_ASSERT_UINT32_WITHIN(5000, cfg.warning_timeout_ms, warning_at);
    TEST_ASSERT_UINT32_WITHIN(5000, cfg.shutdown_timeout_ms, shutdown_at);
}

/* A hand waved at the radar (about 2 s of presence) must not cancel the unattended timers. */
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
    TEST_ASSERT_UINT32_WITHIN(TH_STEP_MS, before + 4000, safety_unattended_ms(&h.sm, h.now));
}

void run_kconfig_tests(void)
{
    RUN_TEST(test_kconfig_safety_config_is_valid);
    RUN_TEST(test_kconfig_warning_and_shutdown_timing);
    RUN_TEST(test_kconfig_wave_keeps_unattended);
    RUN_TEST(test_kconfig_burner_off_pan_cooling_is_not_warned);
    RUN_TEST(test_kconfig_burner_on_low_is_warned_and_shut_down);
}
