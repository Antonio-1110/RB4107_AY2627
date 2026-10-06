/* Valve node open/close logic (components/valve_logic). */
#include "unity.h"
#include "valve_logic.h"

#define T0 1000u

static valve_logic_config_t wired_cfg(void)
{
    return (valve_logic_config_t){.mode = VALVE_MODE_WIRED, .open_delay_ms = 500, .close_debounce_ms = 50};
}

static valve_logic_config_t wireless_cfg(void)
{
    return (valve_logic_config_t){.mode = VALVE_MODE_WIRELESS, .link_timeout_ms = 3000};
}

/* Feed the wired input every 20 ms, like the node's loop. */
static void run_line(valve_logic_t *v, bool low, uint32_t *now, uint32_t ms)
{
    for (uint32_t end = *now + ms; *now < end; *now += 20) {
        valve_logic_on_line(v, low, *now);
        valve_logic_tick(v, *now);
    }
}

static void test_valve_starts_closed(void)
{
    valve_logic_t v;
    const valve_logic_config_t cfg = wireless_cfg();
    valve_logic_init(&v, &cfg, T0);
    TEST_ASSERT_FALSE(valve_logic_tick(&v, T0 + 10000)); /* nothing heard: never opens */
    TEST_ASSERT_EQUAL(RB_VALVE_REASON_BOOT, v.reason);
}

static void test_wired_opens_after_delay_and_closes_on_high(void)
{
    valve_logic_t v;
    const valve_logic_config_t cfg = wired_cfg();
    uint32_t now = T0;
    valve_logic_init(&v, &cfg, now);
    run_line(&v, true, &now, 400);
    TEST_ASSERT_FALSE(v.open); /* low, but not for 500 ms yet */
    run_line(&v, true, &now, 200);
    TEST_ASSERT_TRUE(v.open);

    run_line(&v, false, &now, 20); /* a 20 ms glitch is filtered */
    run_line(&v, true, &now, 20);
    TEST_ASSERT_TRUE(v.open);

    run_line(&v, false, &now, 100); /* output off / wire cut */
    TEST_ASSERT_FALSE(v.open);
    TEST_ASSERT_EQUAL(RB_VALVE_REASON_WIRED_LINE, v.reason);
}

static void test_wireless_keep_open_timeout_and_close(void)
{
    valve_logic_t v;
    const valve_logic_config_t cfg = wireless_cfg();
    valve_logic_init(&v, &cfg, T0);
    valve_logic_on_command(&v, RB_VALVE_CMD_KEEP_OPEN, T0);
    TEST_ASSERT_TRUE(valve_logic_tick(&v, T0));
    valve_logic_on_command(&v, RB_VALVE_CMD_KEEP_OPEN, T0 + 1000);
    TEST_ASSERT_TRUE(valve_logic_tick(&v, T0 + 3900)); /* 2.9 s since the last keep-open */
    TEST_ASSERT_FALSE(valve_logic_tick(&v, T0 + 4000)); /* link lost */
    TEST_ASSERT_EQUAL(RB_VALVE_REASON_LINK_TIMEOUT, v.reason);

    valve_logic_on_command(&v, RB_VALVE_CMD_KEEP_OPEN, T0 + 5000); /* link back, still safe */
    TEST_ASSERT_TRUE(valve_logic_tick(&v, T0 + 5000));
    valve_logic_on_command(&v, RB_VALVE_CMD_CLOSE, T0 + 5100);
    TEST_ASSERT_FALSE(valve_logic_tick(&v, T0 + 5100));
    TEST_ASSERT_EQUAL(RB_VALVE_REASON_COMMAND, v.reason);
    valve_logic_on_command(&v, (rb_valve_cmd_t)77, T0 + 5200); /* unknown command = close */
    TEST_ASSERT_FALSE(valve_logic_tick(&v, T0 + 5200));
}

static void test_latch_keeps_valve_closed(void)
{
    valve_logic_t v;
    valve_logic_config_t cfg = wireless_cfg();
    cfg.latch_closed = true;
    valve_logic_init(&v, &cfg, T0);
    valve_logic_on_command(&v, RB_VALVE_CMD_KEEP_OPEN, T0); /* boot close doesn't latch */
    TEST_ASSERT_TRUE(valve_logic_tick(&v, T0));
    valve_logic_on_command(&v, RB_VALVE_CMD_CLOSE, T0 + 100);
    valve_logic_on_command(&v, RB_VALVE_CMD_KEEP_OPEN, T0 + 200);
    TEST_ASSERT_FALSE(valve_logic_tick(&v, T0 + 200));
    TEST_ASSERT_TRUE(v.latched);
}

static void test_mode_ignores_other_input(void)
{
    valve_logic_t v;
    const valve_logic_config_t cfg = wired_cfg();
    valve_logic_init(&v, &cfg, T0);
    valve_logic_on_command(&v, RB_VALVE_CMD_KEEP_OPEN, T0); /* a wired node has no radio input */
    TEST_ASSERT_FALSE(valve_logic_tick(&v, T0));
}

void run_valve_tests(void)
{
    RUN_TEST(test_valve_starts_closed);
    RUN_TEST(test_wired_opens_after_delay_and_closes_on_high);
    RUN_TEST(test_wireless_keep_open_timeout_and_close);
    RUN_TEST(test_latch_keeps_valve_closed);
    RUN_TEST(test_mode_ignores_other_input);
}
