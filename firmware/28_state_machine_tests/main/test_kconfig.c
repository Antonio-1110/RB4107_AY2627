/*
 * The menuconfig safety settings (what firmware 29 actually runs with) are
 * consistent and give the documented behaviour through the real state machine.
 * (TODO section 10.)
 */
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
    h.in.hot_region_temp_c = cfg.heat_on_temp_c + 20.0f;
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

void run_kconfig_tests(void)
{
    RUN_TEST(test_kconfig_safety_config_is_valid);
    RUN_TEST(test_kconfig_warning_and_shutdown_timing);
}
