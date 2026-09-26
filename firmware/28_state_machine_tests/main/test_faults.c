/* Fault manager: MQTT disconnect/reconnect must never touch the safety state. */
#include "fault_manager.h"
#include "test_harness.h"
#include "unity.h"

static uint32_t s_clock;
static uint32_t fake_now(void) { return s_clock; }

static int s_changes;
static void on_change(fault_id_t id, bool active, int32_t detail, void *ctx) { s_changes++; }

/* Run the state machine with safety_fault wired to the fault manager, as rb_controller_app does. */
static void run(th_t *h, uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += TH_STEP_MS) {
        h->in.safety_fault = fault_any_safety_active();
        th_run(h, TH_STEP_MS);
    }
}

static void test_classification(void)
{
    /* Every sensor node, and every sensor, is safety-critical. */
    TEST_ASSERT_EQUAL(FAULT_CLASS_SAFETY, fault_info(FAULT_PRESENCE_A_NODE_OFFLINE)->fault_class);
    TEST_ASSERT_EQUAL(FAULT_CLASS_SAFETY, fault_info(FAULT_PRESENCE_B_NODE_OFFLINE)->fault_class);
    TEST_ASSERT_EQUAL(FAULT_CLASS_SAFETY, fault_info(FAULT_THERMAL_NODE_OFFLINE)->fault_class);
    TEST_ASSERT_EQUAL(FAULT_CLASS_SAFETY, fault_info(FAULT_PRESENCE_A_UNAVAILABLE)->fault_class);
    TEST_ASSERT_EQUAL(FAULT_CLASS_SAFETY, fault_info(FAULT_PRESENCE_B_UNAVAILABLE)->fault_class);
    TEST_ASSERT_EQUAL(FAULT_CLASS_SAFETY, fault_info(FAULT_THERMAL_UNAVAILABLE)->fault_class);
    TEST_ASSERT_EQUAL(FAULT_CLASS_TELEMETRY, fault_info(FAULT_ESPNOW_UNKNOWN_NODE)->fault_class);
    TEST_ASSERT_EQUAL(FAULT_CLASS_SAFETY, fault_info(FAULT_SHUTDOWN_OUTPUT)->fault_class);
    TEST_ASSERT_EQUAL(FAULT_CLASS_TELEMETRY, fault_info(FAULT_MQTT_DOWN)->fault_class);
    TEST_ASSERT_EQUAL(FAULT_CLASS_TELEMETRY, fault_info(FAULT_NETWORK_DOWN)->fault_class);
    TEST_ASSERT_EQUAL(FAULT_CLASS_TELEMETRY, fault_info(FAULT_RTC)->fault_class);
}

static void test_raise_clear_bookkeeping(void)
{
    fault_manager_init(fake_now, on_change, NULL);
    s_changes = 0;
    s_clock = 1234;
    fault_raise(FAULT_RTC, 7);
    fault_raise(FAULT_RTC, 8); /* already active: no second change */
    TEST_ASSERT_EQUAL(1, s_changes);
    fault_status_t st;
    fault_get_status(FAULT_RTC, &st);
    TEST_ASSERT_TRUE(st.active);
    TEST_ASSERT_EQUAL_UINT32(1234, st.since_ms);
    TEST_ASSERT_EQUAL_INT32(8, st.detail);
    TEST_ASSERT_FALSE(fault_any_safety_active());
    fault_clear(FAULT_RTC);
    TEST_ASSERT_EQUAL(2, s_changes);
    TEST_ASSERT_EQUAL_UINT32(0, fault_active_mask());
}

static void test_mqtt_disconnect_and_reconnect_do_not_affect_safety(void)
{
    fault_manager_init(fake_now, NULL, NULL);
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    h.in.presence = RB_FALSE;
    run(&h, 20000);
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    const uint32_t transitions = h.sm.transitions;
    const uint32_t unattended_before = safety_unattended_ms(&h.sm, h.now);

    fault_raise(FAULT_MQTT_DOWN, 0);    /* broker disappears */
    fault_raise(FAULT_NETWORK_DOWN, 0); /* and the MacBook with it */
    run(&h, 10000);
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    TEST_ASSERT_EQUAL_UINT32(transitions, h.sm.transitions);
    TEST_ASSERT_UINT32_WITHIN(TH_STEP_MS, unattended_before + 10000, safety_unattended_ms(&h.sm, h.now));

    /* Timers keep running through the outage: WARNING and SHUTDOWN still happen on time. */
    run(&h, cfg.shutdown_timeout_ms - 30000 + TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_SHUTDOWN);
    TEST_ASSERT_TRUE(h.out.shutdown);

    fault_clear(FAULT_NETWORK_DOWN); /* reconnect: nothing resets */
    fault_clear(FAULT_MQTT_DOWN);
    run(&h, 5000);
    TH_EXPECT(&h, SAFETY_SHUTDOWN);
}

static void test_safety_fault_enters_fault_state(void)
{
    fault_manager_init(fake_now, NULL, NULL);
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    th_cook(&h);
    fault_raise(FAULT_SHUTDOWN_OUTPUT, 0);
    run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_FAULT);
    fault_clear(FAULT_SHUTDOWN_OUTPUT);
    run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_MONITORING);
}

void run_fault_tests(void)
{
    RUN_TEST(test_classification);
    RUN_TEST(test_raise_clear_bookkeeping);
    RUN_TEST(test_mqtt_disconnect_and_reconnect_do_not_affect_safety);
    RUN_TEST(test_safety_fault_enters_fault_state);
}
