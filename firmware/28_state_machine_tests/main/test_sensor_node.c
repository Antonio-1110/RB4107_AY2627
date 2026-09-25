/* Sensor-node tracking and health (sections 7-8), and their effect on the state machine. */
#include <math.h>
#include "rb_protocol.h"
#include "sensor_node.h"
#include "test_harness.h"
#include "unity.h"

#define NODE 1u

static const sensor_node_health_config_t HEALTH = {.stale_timeout_ms = 2000, .offline_timeout_ms = 10000};

static rb_packet_t data_packet(uint32_t seq, uint32_t uptime, bool present)
{
    rb_packet_t p = {.type = RB_MSG_SENSOR_DATA, .node_id = NODE, .sequence = seq, .uptime_ms = uptime};
    p.body.sensor.presence = (presence_reading_t){.valid = true, .presence_detected = present, .distance_m = NAN};
    p.body.sensor.thermal = (thermal_reading_t){.valid = true, .hot_region_temp_c = 120.0f};
    return p;
}

static void test_sequence_tracking(void)
{
    sensor_node_state_t n;
    sensor_node_init(&n, NODE);
    rb_packet_t p = data_packet(10, 1000, true);
    TEST_ASSERT_EQUAL(NODE_SEQ_FIRST, sensor_node_on_packet(&n, &p, 0));
    p = data_packet(11, 1100, true);
    TEST_ASSERT_EQUAL(NODE_SEQ_OK, sensor_node_on_packet(&n, &p, 100));
    TEST_ASSERT_EQUAL(NODE_SEQ_DUPLICATE, sensor_node_on_packet(&n, &p, 150));
    p = data_packet(15, 1500, true);
    TEST_ASSERT_EQUAL(NODE_SEQ_GAP, sensor_node_on_packet(&n, &p, 500));
    TEST_ASSERT_EQUAL_UINT32(3, n.missed);
    p = data_packet(13, 1300, false); /* late packet: must not overwrite newer data */
    TEST_ASSERT_EQUAL(NODE_SEQ_OUT_OF_ORDER, sensor_node_on_packet(&n, &p, 600));
    TEST_ASSERT_TRUE(n.latest_data.presence.presence_detected);
    p = data_packet(1, 200, true); /* node rebooted */
    TEST_ASSERT_EQUAL(NODE_SEQ_NODE_RESTART, sensor_node_on_packet(&n, &p, 700));
    TEST_ASSERT_EQUAL_UINT32(1, n.restarts);
    p = data_packet(2, 300, true);
    p.node_id = 99;
    TEST_ASSERT_EQUAL(NODE_SEQ_WRONG_NODE, sensor_node_on_packet(&n, &p, 800));
    TEST_ASSERT_EQUAL_UINT32(1, n.wrong_node);
}

static void test_sequence_wraparound(void)
{
    sensor_node_state_t n;
    sensor_node_init(&n, NODE);
    rb_packet_t p = data_packet(UINT32_MAX, 1000, true);
    sensor_node_on_packet(&n, &p, 0);
    p = data_packet(0, 1100, true);
    TEST_ASSERT_EQUAL(NODE_SEQ_OK, sensor_node_on_packet(&n, &p, 100));
}

static void test_health_online_stale_offline_and_recovery(void)
{
    sensor_node_state_t n;
    sensor_node_init(&n, NODE);
    node_inputs_t in;
    TEST_ASSERT_EQUAL_UINT32(0, sensor_node_evaluate(&n, &HEALTH, 0));
    sensor_node_inputs(&n, &in);
    TEST_ASSERT_EQUAL(RB_UNKNOWN, in.presence); /* never seen: unknown, not absent */

    rb_packet_t p = data_packet(1, 1000, false);
    sensor_node_on_packet(&n, &p, 1000);
    uint32_t ev = sensor_node_evaluate(&n, &HEALTH, 1000);
    TEST_ASSERT_TRUE(ev & NODE_EVT_ONLINE);
    TEST_ASSERT_TRUE(ev & NODE_EVT_PRESENCE_VALID);
    TEST_ASSERT_TRUE(ev & NODE_EVT_THERMAL_VALID);
    sensor_node_inputs(&n, &in);
    TEST_ASSERT_EQUAL(RB_FALSE, in.presence);
    TEST_ASSERT_TRUE(in.thermal_valid);

    /* The node disappears. */
    ev = sensor_node_evaluate(&n, &HEALTH, 1000 + HEALTH.stale_timeout_ms);
    TEST_ASSERT_TRUE(ev & NODE_EVT_STALE);
    TEST_ASSERT_TRUE(ev & NODE_EVT_PRESENCE_INVALID);
    TEST_ASSERT_TRUE(ev & NODE_EVT_THERMAL_INVALID);
    sensor_node_inputs(&n, &in);
    TEST_ASSERT_EQUAL(RB_UNKNOWN, in.presence); /* stale data is not "absent" */
    TEST_ASSERT_FALSE(in.thermal_valid);
    TEST_ASSERT_TRUE(isnan(in.hot_region_temp_c));
    ev = sensor_node_evaluate(&n, &HEALTH, 1000 + HEALTH.offline_timeout_ms);
    TEST_ASSERT_EQUAL_UINT32(NODE_EVT_OFFLINE, ev);

    /* The node returns. */
    p = data_packet(2, 12000, true);
    sensor_node_on_packet(&n, &p, 12000);
    ev = sensor_node_evaluate(&n, &HEALTH, 12000);
    TEST_ASSERT_TRUE(ev & NODE_EVT_ONLINE);
    TEST_ASSERT_TRUE(ev & NODE_EVT_PRESENCE_VALID);
    sensor_node_inputs(&n, &in);
    TEST_ASSERT_EQUAL(RB_TRUE, in.presence);
}

static void test_invalid_reading_from_live_node(void)
{
    sensor_node_state_t n;
    sensor_node_init(&n, NODE);
    rb_packet_t p = data_packet(1, 1000, true);
    p.body.sensor.presence.valid = false; /* C4002 fault reported by an otherwise healthy node */
    sensor_node_on_packet(&n, &p, 1000);
    const uint32_t ev = sensor_node_evaluate(&n, &HEALTH, 1000);
    TEST_ASSERT_TRUE(ev & NODE_EVT_ONLINE);
    TEST_ASSERT_FALSE(ev & NODE_EVT_PRESENCE_VALID);
    node_inputs_t in;
    sensor_node_inputs(&n, &in);
    TEST_ASSERT_EQUAL(RB_UNKNOWN, in.presence);
    TEST_ASSERT_TRUE(in.thermal_valid);
}

/* Integration: node health feeding the state machine, as the controller's safety task does. */
static void feed(th_t *h, sensor_node_state_t *n)
{
    sensor_node_evaluate(n, &HEALTH, h->now);
    node_inputs_t in;
    sensor_node_inputs(n, &in);
    h->in.presence = in.presence;
    h->in.thermal_valid = in.thermal_valid;
    h->in.hot_region_temp_c = in.hot_region_temp_c;
}

static void test_node_disappears_and_returns(void)
{
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    sensor_node_state_t n;
    sensor_node_init(&n, NODE);
    uint32_t seq = 0;

    /* Node online: person present, hob hot, packets every 500 ms for 10 s. */
    for (int i = 0; i < 20; i++) {
        rb_packet_t p = data_packet(++seq, h.now, true);
        sensor_node_on_packet(&n, &p, h.now);
        feed(&h, &n);
        th_run(&h, 500);
    }
    TH_EXPECT(&h, SAFETY_MONITORING);

    /* Node disappears: FAULT once the data goes stale. */
    for (int i = 0; i < 30; i++) {
        feed(&h, &n);
        th_run(&h, TH_STEP_MS);
    }
    TH_EXPECT(&h, SAFETY_FAULT);
    TEST_ASSERT_EQUAL(SAFETY_BUZZER_FAULT, h.out.buzzer);

    /* Node returns with the person present. */
    rb_packet_t p = data_packet(++seq, h.now, true);
    sensor_node_on_packet(&n, &p, h.now);
    feed(&h, &n);
    th_run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_MONITORING);
}

void run_sensor_node_tests(void)
{
    RUN_TEST(test_sequence_tracking);
    RUN_TEST(test_sequence_wraparound);
    RUN_TEST(test_health_online_stale_offline_and_recovery);
    RUN_TEST(test_invalid_reading_from_live_node);
    RUN_TEST(test_node_disappears_and_returns);
}
