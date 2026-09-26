/*
 * Sensor-node tracking and health (sections 7-8), combining the three nodes
 * (two presence, one thermal), and their effect on the state machine.
 */
#include <math.h>
#include "rb_protocol.h"
#include "sensor_node.h"
#include "test_harness.h"
#include "unity.h"

#define NODE_A 1u
#define NODE_B 2u
#define NODE_T 3u

static const sensor_node_health_config_t HEALTH = {.stale_timeout_ms = 2000, .offline_timeout_ms = 10000};
static const node_set_config_t NODES = {
    .presence_node_ids = {NODE_A, NODE_B},
    .presence_node_count = 2,
    .thermal_node_id = NODE_T,
};

static rb_packet_t presence_packet(uint32_t node, uint32_t seq, uint32_t uptime, bool present)
{
    rb_packet_t p = {.type = RB_MSG_PRESENCE_DATA, .role = RB_NODE_ROLE_PRESENCE, .node_id = node, .sequence = seq,
                     .uptime_ms = uptime};
    p.body.presence = (presence_reading_t){.valid = true, .presence_detected = present, .distance_m = NAN};
    return p;
}

static rb_packet_t thermal_packet(uint32_t seq, uint32_t uptime, float hot_c)
{
    rb_packet_t p = {.type = RB_MSG_THERMAL_DATA, .role = RB_NODE_ROLE_THERMAL, .node_id = NODE_T, .sequence = seq,
                     .uptime_ms = uptime};
    p.body.thermal = (thermal_reading_t){.valid = true, .hot_region_temp_c = hot_c};
    return p;
}

/* ---- One node ---- */

static void test_sequence_tracking(void)
{
    sensor_node_state_t n;
    sensor_node_init(&n, NODE_A, RB_NODE_ROLE_PRESENCE);
    rb_packet_t p = presence_packet(NODE_A, 10, 1000, true);
    TEST_ASSERT_EQUAL(NODE_SEQ_FIRST, sensor_node_on_packet(&n, &p, 0));
    p = presence_packet(NODE_A, 11, 1100, true);
    TEST_ASSERT_EQUAL(NODE_SEQ_OK, sensor_node_on_packet(&n, &p, 100));
    TEST_ASSERT_EQUAL(NODE_SEQ_DUPLICATE, sensor_node_on_packet(&n, &p, 150));
    p = presence_packet(NODE_A, 15, 1500, true);
    TEST_ASSERT_EQUAL(NODE_SEQ_GAP, sensor_node_on_packet(&n, &p, 500));
    TEST_ASSERT_EQUAL_UINT32(3, n.missed);
    p = presence_packet(NODE_A, 13, 1300, false); /* late packet: must not overwrite newer data */
    TEST_ASSERT_EQUAL(NODE_SEQ_OUT_OF_ORDER, sensor_node_on_packet(&n, &p, 600));
    TEST_ASSERT_TRUE(n.presence.presence_detected);
    p = presence_packet(NODE_A, 1, 200, true); /* node rebooted */
    TEST_ASSERT_EQUAL(NODE_SEQ_NODE_RESTART, sensor_node_on_packet(&n, &p, 700));
    TEST_ASSERT_EQUAL_UINT32(1, n.restarts);
    p = presence_packet(99, 2, 300, true);
    TEST_ASSERT_EQUAL(NODE_SEQ_WRONG_NODE, sensor_node_on_packet(&n, &p, 800));
}

static void test_sequence_wraparound(void)
{
    sensor_node_state_t n;
    sensor_node_init(&n, NODE_A, RB_NODE_ROLE_PRESENCE);
    rb_packet_t p = presence_packet(NODE_A, UINT32_MAX, 1000, true);
    sensor_node_on_packet(&n, &p, 0);
    p = presence_packet(NODE_A, 0, 1100, true);
    TEST_ASSERT_EQUAL(NODE_SEQ_OK, sensor_node_on_packet(&n, &p, 100));
}

static void test_wrong_role_is_dropped(void)
{
    /* A thermal node flashed with presence node A's ID must never count as a radar. */
    sensor_node_state_t n;
    sensor_node_init(&n, NODE_A, RB_NODE_ROLE_PRESENCE);
    rb_packet_t p = thermal_packet(1, 1000, 25.0f);
    p.node_id = NODE_A;
    TEST_ASSERT_EQUAL(NODE_SEQ_WRONG_ROLE, sensor_node_on_packet(&n, &p, 1000));
    TEST_ASSERT_EQUAL_UINT32(1, n.wrong_role);
    TEST_ASSERT_EQUAL_UINT32(0, n.packets);
    sensor_node_evaluate(&n, &HEALTH, 1000);
    TEST_ASSERT_EQUAL(NODE_LINK_NEVER_SEEN, n.link);
}

static void test_health_online_stale_offline_and_recovery(void)
{
    sensor_node_state_t n;
    sensor_node_init(&n, NODE_A, RB_NODE_ROLE_PRESENCE);
    TEST_ASSERT_EQUAL_UINT32(0, sensor_node_evaluate(&n, &HEALTH, 0));
    TEST_ASSERT_EQUAL(RB_UNKNOWN, sensor_node_presence(&n)); /* never seen: unknown, not absent */

    rb_packet_t p = presence_packet(NODE_A, 1, 1000, false);
    sensor_node_on_packet(&n, &p, 1000);
    uint32_t ev = sensor_node_evaluate(&n, &HEALTH, 1000);
    TEST_ASSERT_TRUE(ev & NODE_EVT_ONLINE);
    TEST_ASSERT_TRUE(ev & NODE_EVT_SENSOR_VALID);
    TEST_ASSERT_EQUAL(RB_FALSE, sensor_node_presence(&n));

    /* The node disappears. */
    ev = sensor_node_evaluate(&n, &HEALTH, 1000 + HEALTH.stale_timeout_ms);
    TEST_ASSERT_TRUE(ev & NODE_EVT_STALE);
    TEST_ASSERT_TRUE(ev & NODE_EVT_SENSOR_INVALID);
    TEST_ASSERT_EQUAL(RB_UNKNOWN, sensor_node_presence(&n)); /* stale data is not "absent" */
    ev = sensor_node_evaluate(&n, &HEALTH, 1000 + HEALTH.offline_timeout_ms);
    TEST_ASSERT_EQUAL_UINT32(NODE_EVT_OFFLINE, ev);

    /* The node returns. */
    p = presence_packet(NODE_A, 2, 12000, true);
    sensor_node_on_packet(&n, &p, 12000);
    ev = sensor_node_evaluate(&n, &HEALTH, 12000);
    TEST_ASSERT_TRUE(ev & NODE_EVT_ONLINE);
    TEST_ASSERT_TRUE(ev & NODE_EVT_SENSOR_VALID);
    TEST_ASSERT_EQUAL(RB_TRUE, sensor_node_presence(&n));
}

static void test_invalid_reading_from_live_node(void)
{
    sensor_node_state_t n;
    sensor_node_init(&n, NODE_A, RB_NODE_ROLE_PRESENCE);
    rb_packet_t p = presence_packet(NODE_A, 1, 1000, true);
    p.body.presence.valid = false; /* C4002 fault reported by an otherwise healthy node */
    sensor_node_on_packet(&n, &p, 1000);
    const uint32_t ev = sensor_node_evaluate(&n, &HEALTH, 1000);
    TEST_ASSERT_TRUE(ev & NODE_EVT_ONLINE);
    TEST_ASSERT_FALSE(ev & NODE_EVT_SENSOR_VALID);
    TEST_ASSERT_EQUAL(RB_UNKNOWN, sensor_node_presence(&n));
}

/* ---- Combining presence ---- */

static void test_presence_fuse_truth_table(void)
{
    const rb_tristate_t U = RB_UNKNOWN, F = RB_FALSE, T = RB_TRUE;
    const struct {
        rb_tristate_t a, b, expected;
    } rows[] = {
        {T, T, T}, {T, F, T}, {F, T, T}, {T, U, T}, {U, T, T}, /* anyone sees a person: present */
        {F, F, F},                                             /* both valid and empty: absent */
        {F, U, U}, {U, F, U}, {U, U, U},                       /* one missing and no person seen: unknown */
    };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        const rb_tristate_t r[2] = {rows[i].a, rows[i].b};
        TEST_ASSERT_EQUAL_MESSAGE(rows[i].expected, presence_fuse(r, 2), "row");
    }
    const rb_tristate_t one[1] = {F};
    TEST_ASSERT_EQUAL(RB_FALSE, presence_fuse(one, 1));
    TEST_ASSERT_EQUAL(RB_UNKNOWN, presence_fuse(one, 0)); /* no sensors: never "absent" */
}

static void test_node_set_config_validation(void)
{
    TEST_ASSERT_TRUE(node_set_config_valid(&NODES));
    node_set_config_t c = NODES;
    c.presence_node_ids[1] = NODE_A; /* two radars with one ID */
    TEST_ASSERT_FALSE(node_set_config_valid(&c));
    c = NODES;
    c.thermal_node_id = NODE_B; /* thermal node shares a radar's ID */
    TEST_ASSERT_FALSE(node_set_config_valid(&c));
    c = NODES;
    c.presence_node_count = 0;
    TEST_ASSERT_FALSE(node_set_config_valid(&c));
    c = NODES;
    c.presence_node_count = 1;
    c.presence_node_ids[1] = NODE_T; /* ignored when only one radar is configured */
    TEST_ASSERT_TRUE(node_set_config_valid(&c));
}

static void test_node_set_routes_and_combines(void)
{
    node_set_t set;
    node_set_init(&set, &NODES);
    node_inputs_t in;
    uint32_t ev[NODE_SLOT_COUNT];

    node_set_evaluate(&set, &HEALTH, 0, ev);
    node_set_inputs(&set, &in);
    TEST_ASSERT_EQUAL(RB_UNKNOWN, in.presence);
    TEST_ASSERT_FALSE(in.thermal_valid);

    node_slot_t slot;
    rb_packet_t p = presence_packet(NODE_A, 1, 100, false);
    TEST_ASSERT_EQUAL(NODE_SEQ_FIRST, node_set_on_packet(&set, &p, 1000, &slot));
    TEST_ASSERT_EQUAL(NODE_SLOT_PRESENCE_A, slot);
    p = presence_packet(NODE_B, 1, 100, false);
    node_set_on_packet(&set, &p, 1000, &slot);
    TEST_ASSERT_EQUAL(NODE_SLOT_PRESENCE_B, slot);
    p = thermal_packet(1, 100, 120.0f);
    node_set_on_packet(&set, &p, 1000, &slot);
    TEST_ASSERT_EQUAL(NODE_SLOT_THERMAL, slot);
    p = presence_packet(42, 1, 100, true);
    TEST_ASSERT_EQUAL(NODE_SEQ_WRONG_NODE, node_set_on_packet(&set, &p, 1000, &slot));
    TEST_ASSERT_EQUAL(NODE_SLOT_COUNT, slot);
    TEST_ASSERT_EQUAL_UINT32(1, set.unknown_node);

    node_set_evaluate(&set, &HEALTH, 1000, ev);
    for (int i = 0; i < NODE_SLOT_COUNT; i++) {
        TEST_ASSERT_TRUE(ev[i] & NODE_EVT_ONLINE);
        TEST_ASSERT_TRUE(ev[i] & NODE_EVT_SENSOR_VALID);
    }
    node_set_inputs(&set, &in);
    TEST_ASSERT_EQUAL(RB_FALSE, in.presence); /* both radars valid and empty */
    TEST_ASSERT_TRUE(in.thermal_valid);
    TEST_ASSERT_EQUAL_FLOAT(120.0f, in.hot_region_temp_c);

    /* Only radar B sees the person (e.g. standing out of A's field of view). */
    p = presence_packet(NODE_B, 2, 200, true);
    node_set_on_packet(&set, &p, 1100, NULL);
    node_set_evaluate(&set, &HEALTH, 1100, ev);
    node_set_inputs(&set, &in);
    TEST_ASSERT_EQUAL(RB_TRUE, in.presence);
    TEST_ASSERT_EQUAL(RB_FALSE, in.presence_each[0]);
    TEST_ASSERT_EQUAL(RB_TRUE, in.presence_each[1]);

    /* Radar B says absent but radar A has gone quiet: UNKNOWN, never ABSENT. */
    p = presence_packet(NODE_B, 3, 300, false);
    node_set_on_packet(&set, &p, 2500, NULL);
    p = thermal_packet(2, 300, 120.0f);
    node_set_on_packet(&set, &p, 2500, NULL);
    node_set_evaluate(&set, &HEALTH, 3000, ev);
    TEST_ASSERT_TRUE(ev[NODE_SLOT_PRESENCE_A] & NODE_EVT_STALE);
    TEST_ASSERT_EQUAL_UINT32(0, ev[NODE_SLOT_PRESENCE_B] & (NODE_EVT_STALE | NODE_EVT_SENSOR_INVALID));
    node_set_inputs(&set, &in);
    TEST_ASSERT_EQUAL(RB_UNKNOWN, in.presence_each[0]);
    TEST_ASSERT_EQUAL(RB_FALSE, in.presence_each[1]);
    TEST_ASSERT_EQUAL(RB_UNKNOWN, in.presence);
    TEST_ASSERT_TRUE(in.thermal_valid); /* the thermal node is unaffected */
}

static void test_node_set_single_radar(void)
{
    node_set_config_t c = NODES;
    c.presence_node_count = 1;
    node_set_t set;
    node_set_init(&set, &c);
    TEST_ASSERT_FALSE(set.enabled[NODE_SLOT_PRESENCE_B]);

    rb_packet_t p = presence_packet(NODE_B, 1, 100, true); /* B not configured: dropped */
    TEST_ASSERT_EQUAL(NODE_SEQ_WRONG_NODE, node_set_on_packet(&set, &p, 1000, NULL));
    p = presence_packet(NODE_A, 1, 100, false);
    node_set_on_packet(&set, &p, 1000, NULL);
    uint32_t ev[NODE_SLOT_COUNT];
    node_set_evaluate(&set, &HEALTH, 1000, ev);
    TEST_ASSERT_EQUAL_UINT32(0, ev[NODE_SLOT_PRESENCE_B]);
    node_inputs_t in;
    node_set_inputs(&set, &in);
    TEST_ASSERT_EQUAL(RB_FALSE, in.presence); /* one radar, valid and empty */
}

/* ---- Integration: the nodes feeding the state machine, as the controller's safety task does ---- */

typedef struct {
    node_set_t set;
    uint32_t seq[NODE_SLOT_COUNT];
} nodes_t;

static void nodes_send(nodes_t *n, uint32_t now, bool a_on, bool b_on, bool present, float hot_c)
{
    if (a_on) {
        rb_packet_t p = presence_packet(NODE_A, ++n->seq[0], now, present);
        node_set_on_packet(&n->set, &p, now, NULL);
    }
    if (b_on) {
        rb_packet_t p = presence_packet(NODE_B, ++n->seq[1], now, present);
        node_set_on_packet(&n->set, &p, now, NULL);
    }
    rb_packet_t p = thermal_packet(++n->seq[2], now, hot_c);
    node_set_on_packet(&n->set, &p, now, NULL);
}

static void feed(th_t *h, nodes_t *n)
{
    uint32_t ev[NODE_SLOT_COUNT];
    node_set_evaluate(&n->set, &HEALTH, h->now, ev);
    node_inputs_t in;
    node_set_inputs(&n->set, &in);
    h->in.presence = in.presence;
    h->in.thermal_valid = in.thermal_valid;
    h->in.hot_region_temp_c = in.hot_region_temp_c;
}

static void test_radar_disappears_and_returns(void)
{
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    nodes_t n = {0};
    node_set_init(&n.set, &NODES);

    /* All nodes online: person present, hob hot, packets every 500 ms for 10 s. */
    for (int i = 0; i < 20; i++) {
        nodes_send(&n, h.now, true, true, true, 120.0f);
        feed(&h, &n);
        th_run(&h, 500);
    }
    TH_EXPECT(&h, SAFETY_MONITORING);

    /* The person leaves while radar A goes silent: B's "absent" alone is not enough. */
    for (int i = 0; i < 30; i++) {
        nodes_send(&n, h.now, false, true, false, 120.0f);
        feed(&h, &n);
        th_run(&h, TH_STEP_MS);
    }
    TH_EXPECT(&h, SAFETY_FAULT);
    TEST_ASSERT_EQUAL(SAFETY_BUZZER_FAULT, h.out.buzzer);

    /* Radar A returns and the person is back. */
    nodes_send(&n, h.now, true, true, true, 120.0f);
    feed(&h, &n);
    th_run(&h, TH_STEP_MS);
    TH_EXPECT(&h, SAFETY_MONITORING);
}

static void test_either_radar_keeps_attended(void)
{
    th_t h;
    const safety_config_t cfg = th_default_config();
    th_start(&h, &cfg, 0);
    nodes_t n = {0};
    node_set_init(&n.set, &NODES);
    for (int i = 0; i < 20; i++) {
        nodes_send(&n, h.now, true, true, true, 120.0f);
        feed(&h, &n);
        th_run(&h, 500);
    }
    TH_EXPECT(&h, SAFETY_MONITORING);

    /* Only radar B sees the cook, for longer than the warning timeout: still attended. */
    for (uint32_t t = 0; t < cfg.warning_timeout_ms + 10000; t += 500) {
        rb_packet_t p = presence_packet(NODE_A, ++n.seq[0], h.now, false);
        node_set_on_packet(&n.set, &p, h.now, NULL);
        p = presence_packet(NODE_B, ++n.seq[1], h.now, true);
        node_set_on_packet(&n.set, &p, h.now, NULL);
        p = thermal_packet(++n.seq[2], h.now, 120.0f);
        node_set_on_packet(&n.set, &p, h.now, NULL);
        feed(&h, &n);
        th_run(&h, 500);
    }
    TH_EXPECT(&h, SAFETY_MONITORING);
}

void run_sensor_node_tests(void)
{
    RUN_TEST(test_sequence_tracking);
    RUN_TEST(test_sequence_wraparound);
    RUN_TEST(test_wrong_role_is_dropped);
    RUN_TEST(test_health_online_stale_offline_and_recovery);
    RUN_TEST(test_invalid_reading_from_live_node);
    RUN_TEST(test_presence_fuse_truth_table);
    RUN_TEST(test_node_set_config_validation);
    RUN_TEST(test_node_set_routes_and_combines);
    RUN_TEST(test_node_set_single_radar);
    RUN_TEST(test_radar_disappears_and_returns);
    RUN_TEST(test_either_radar_keeps_attended);
}
