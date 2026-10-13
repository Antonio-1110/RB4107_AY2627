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
    p.body.thermal = (thermal_reading_t){.valid = true, .max_temp_c = hot_c, .hot_region_temp_c = hot_c};
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

static void test_repeated_restarts_are_flagged(void)
{
    const sensor_node_health_config_t cfg = {
        .stale_timeout_ms = 2000, .offline_timeout_ms = 10000, .restart_limit = 3, .restart_window_ms = 60000};
    sensor_node_state_t n;
    sensor_node_init(&n, NODE_A, RB_NODE_ROLE_PRESENCE);
    uint32_t now = 0, seq = 1;
    rb_packet_t p = presence_packet(NODE_A, seq++, 5000, true);
    sensor_node_on_packet(&n, &p, now);
    sensor_node_evaluate(&n, &cfg, now);

    /* Two reboots in a minute: still below the limit. */
    for (int i = 0; i < 2; i++) {
        now += 10000;
        p = presence_packet(NODE_A, 1, 100, true);
        TEST_ASSERT_EQUAL(NODE_SEQ_NODE_RESTART, sensor_node_on_packet(&n, &p, now));
        p = presence_packet(NODE_A, 2, 5000, true);
        sensor_node_on_packet(&n, &p, now);
        TEST_ASSERT_FALSE(sensor_node_evaluate(&n, &cfg, now) & NODE_EVT_RESTARTING);
    }
    /* The third one within the window raises it. */
    now += 10000;
    p = presence_packet(NODE_A, 1, 100, true);
    sensor_node_on_packet(&n, &p, now);
    TEST_ASSERT_TRUE(sensor_node_evaluate(&n, &cfg, now) & NODE_EVT_RESTARTING);
    TEST_ASSERT_TRUE(n.restarting);
    TEST_ASSERT_EQUAL_UINT32(0, sensor_node_evaluate(&n, &cfg, now) & (NODE_EVT_RESTARTING | NODE_EVT_RESTARTS_SETTLED));

    /* Once the window has passed without reboots it settles (packets keep the link up). */
    for (int i = 0; i < 60; i++) {
        now += 1000;
        p = presence_packet(NODE_A, 2 + (uint32_t)i, 200 + 1000 * (uint32_t)i, true);
        sensor_node_on_packet(&n, &p, now);
        if (sensor_node_evaluate(&n, &cfg, now) & NODE_EVT_RESTARTS_SETTLED) {
            break;
        }
    }
    TEST_ASSERT_FALSE(n.restarting);

    /* restart_limit 0 turns the check off. */
    const sensor_node_health_config_t off = {.stale_timeout_ms = 2000, .offline_timeout_ms = 10000};
    sensor_node_init(&n, NODE_A, RB_NODE_ROLE_PRESENCE);
    for (int i = 0; i < 10; i++) {
        p = presence_packet(NODE_A, 1, i % 2 ? 100 : 5000, true);
        sensor_node_on_packet(&n, &p, (uint32_t)i * 100);
    }
    TEST_ASSERT_EQUAL_UINT32(0, sensor_node_evaluate(&n, &off, 1000) & NODE_EVT_RESTARTING);
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
    h->in.cooking_temp_c = in.max_temp_c; /* the hottest pixel decides cooking */
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

    /* The person leaves while radar A goes silent: B's "absent" alone is not enough to call it
       absent, so presence is UNKNOWN. The hob is hot, so UNKNOWN counts as absent and the timeline runs. */
    for (int i = 0; i < 60; i++) { /* 6 s: A goes stale after 2 s, then the 2 s absence debounce */
        nodes_send(&n, h.now, false, true, false, 120.0f);
        feed(&h, &n);
        th_run(&h, TH_STEP_MS);
    }
    TH_EXPECT(&h, SAFETY_UNATTENDED);
    TEST_ASSERT_TRUE(h.sm.presence_unknown);
    TEST_ASSERT_EQUAL(SAFETY_BUZZER_OFF, h.out.buzzer);

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

static void test_presence_fuse_all_truth_table(void)
{
    const rb_tristate_t U = RB_UNKNOWN, F = RB_FALSE, T = RB_TRUE;
    const rb_tristate_t tt[] = {T, T}, tf[] = {T, F}, ft[] = {F, T}, ff[] = {F, F}, tu[] = {T, U}, ut[] = {U, T},
                        fu[] = {F, U}, uu[] = {U, U};
    TEST_ASSERT_EQUAL(RB_TRUE, presence_fuse_all(tt, 2));   /* both see someone */
    TEST_ASSERT_EQUAL(RB_FALSE, presence_fuse_all(tf, 2));  /* only one sees someone: not at the stove */
    TEST_ASSERT_EQUAL(RB_FALSE, presence_fuse_all(ft, 2));
    TEST_ASSERT_EQUAL(RB_FALSE, presence_fuse_all(ff, 2));
    TEST_ASSERT_EQUAL(RB_TRUE, presence_fuse_all(tu, 2));   /* the other radar is down: asked of the one that works */
    TEST_ASSERT_EQUAL(RB_TRUE, presence_fuse_all(ut, 2));
    TEST_ASSERT_EQUAL(RB_FALSE, presence_fuse_all(fu, 2));
    TEST_ASSERT_EQUAL(RB_UNKNOWN, presence_fuse_all(uu, 2)); /* no radar works */
    TEST_ASSERT_EQUAL(RB_TRUE, presence_fuse_all(tt, 1));   /* one radar configured */
    TEST_ASSERT_EQUAL(RB_UNKNOWN, presence_fuse_all(tt, 0));
}

static void test_node_set_require_all_and_nearby(void)
{
    nodes_t n = {0};
    node_set_config_t cfg = NODES;
    cfg.require_all = true;
    node_set_init(&n.set, &cfg);
    uint32_t ev[NODE_SLOT_COUNT];
    rb_packet_t a = presence_packet(NODE_A, 1, 1000, true), b = presence_packet(NODE_B, 1, 1000, false);
    rb_packet_t t = thermal_packet(1, 1000, 30.0f);
    node_set_on_packet(&n.set, &a, 1000, NULL);
    node_set_on_packet(&n.set, &b, 1000, NULL);
    node_set_on_packet(&n.set, &t, 1000, NULL);
    node_set_evaluate(&n.set, &HEALTH, 1000, ev);
    node_inputs_t in;
    node_set_inputs(&n.set, &in);
    TEST_ASSERT_EQUAL(RB_FALSE, in.presence);     /* radar A sees someone, B does not: not at the stove */
    TEST_ASSERT_EQUAL(RB_TRUE, in.presence_any);  /* but somebody is near */
    b = presence_packet(NODE_B, 2, 1500, true);
    node_set_on_packet(&n.set, &b, 1500, NULL);
    node_set_evaluate(&n.set, &HEALTH, 1500, ev);
    node_set_inputs(&n.set, &in);
    TEST_ASSERT_EQUAL(RB_TRUE, in.presence);
    n.set.require_all = false; /* the owner may switch the fusion at run time */
    a = presence_packet(NODE_A, 2, 2000, true);
    b = presence_packet(NODE_B, 3, 2000, false);
    node_set_on_packet(&n.set, &a, 2000, NULL);
    node_set_on_packet(&n.set, &b, 2000, NULL);
    node_set_evaluate(&n.set, &HEALTH, 2000, ev);
    node_set_inputs(&n.set, &in);
    TEST_ASSERT_EQUAL(RB_TRUE, in.presence);
}

/* ---- radar state and distance rules (placeholder values, simulated sequences, not real walks) ---- */

static const radar_rules_config_t RULES = {.quiet_gap_ms = 1500, .dwell_ms = 4000, .stationary_min_ms = 1500,
                                           .settle_window_ms = 4000, .settle_span_cm = 60, .zone_max_cm = 0};

enum { N = 0, M = 1, S = 2 };

/* One sample every 500 ms from t, as the radar reports it. */
static uint32_t feed_samples(radar_episode_t *ep, const radar_rules_config_t *cfg, uint32_t t, const uint8_t *state,
                             const int32_t *cm, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        radar_episode_update(ep, cfg, t, state[i], cm[i]);
        t += 500;
    }
    return t;
}

static void test_a_walker_is_passing_not_dwelling(void)
{
    radar_episode_t ep = {0};
    /* none, moving across many distances, a moment of "stationary" as they leave the lobe, none */
    const uint8_t st[] = {N, M, M, M, M, M, S, N, N, N, N};
    const int32_t cm[] = {-1, 320, 260, 190, 130, 90, 110, -1, -1, -1, -1};
    uint32_t t = 0;
    for (size_t i = 0; i < sizeof(st); i++, t += 500) {
        radar_episode_update(&ep, &RULES, t, st[i], cm[i]);
        TEST_ASSERT_TRUE(radar_episode_verdict(&ep) != RADAR_VERDICT_DWELLING);
    }
    TEST_ASSERT_EQUAL(RADAR_VERDICT_NONE, radar_episode_verdict(&ep)); /* the episode ended after the quiet gap */
    TEST_ASSERT_EQUAL(RADAR_VERDICT_PASSING, ep.last_verdict);
    TEST_ASSERT_TRUE(ep.last_span_cm >= 200); /* a walker crosses a wide range of distances */
}

static void test_a_slow_walker_who_never_settles_is_passing(void)
{
    radar_episode_t ep = {0};
    uint8_t st[20];
    int32_t cm[20];
    for (int i = 0; i < 20; i++) { /* 10 s of walking 30 cm every half second */
        st[i] = M;
        cm[i] = 400 - 30 * i;
    }
    feed_samples(&ep, &RULES, 0, st, cm, 20);
    TEST_ASSERT_EQUAL(RADAR_VERDICT_PASSING, radar_episode_verdict(&ep));
}

static void test_a_cook_who_arrives_and_settles_is_dwelling(void)
{
    radar_episode_t ep = {0};
    /* arrives (moving, closing in), then stirs and checks the pot at about one metre: stationary with arm movements */
    const uint8_t st[] = {M, M, M, M, S, S, M, S, S, M, S, S, S, M};
    const int32_t cm[] = {250, 200, 150, 110, 100, 100, 105, 100, 95, 100, 100, 105, 100, 100};
    uint32_t t = feed_samples(&ep, &RULES, 0, st, cm, 6);
    TEST_ASSERT_EQUAL(RADAR_VERDICT_PASSING, radar_episode_verdict(&ep)); /* not yet: 3 s in */
    feed_samples(&ep, &RULES, t, st + 6, cm + 6, 8);
    TEST_ASSERT_EQUAL(RADAR_VERDICT_DWELLING, radar_episode_verdict(&ep));
    /* a flicker of "none" shorter than the quiet gap keeps the episode and the verdict */
    radar_episode_update(&ep, &RULES, 7000, N, -1);
    radar_episode_update(&ep, &RULES, 7500, S, 100);
    TEST_ASSERT_EQUAL(RADAR_VERDICT_DWELLING, radar_episode_verdict(&ep));
    /* leaving: none for the quiet gap ends it */
    for (uint32_t t2 = 8000; t2 <= 9500; t2 += 500) {
        radar_episode_update(&ep, &RULES, t2, N, -1);
    }
    TEST_ASSERT_EQUAL(RADAR_VERDICT_NONE, radar_episode_verdict(&ep));
    TEST_ASSERT_EQUAL(RADAR_VERDICT_DWELLING, ep.last_verdict);
}

static void test_standing_still_behind_the_radar_looks_like_a_cook(void)
{
    /* the known limit of one radar: someone who stops and stays where the radar sees a stationary target */
    radar_episode_t ep = {0};
    uint8_t st[20];
    int32_t cm[20];
    for (int i = 0; i < 20; i++) {
        st[i] = S;
        cm[i] = 90;
    }
    feed_samples(&ep, &RULES, 0, st, cm, 20);
    TEST_ASSERT_EQUAL(RADAR_VERDICT_DWELLING, radar_episode_verdict(&ep)); /* only the second radar can tell */
}

static void test_the_cook_zone_limits_what_counts(void)
{
    radar_rules_config_t cfg = RULES;
    cfg.zone_max_cm = 150;
    uint8_t st[20];
    int32_t far[20], near[20];
    for (int i = 0; i < 20; i++) {
        st[i] = S;
        far[i] = 300;
        near[i] = 120;
    }
    radar_episode_t a = {0}, b = {0};
    feed_samples(&a, &cfg, 0, st, far, 20);
    feed_samples(&b, &cfg, 0, st, near, 20);
    TEST_ASSERT_EQUAL(RADAR_VERDICT_PASSING, radar_episode_verdict(&a)); /* 10 s, but out of the zone */
    TEST_ASSERT_EQUAL(RADAR_VERDICT_DWELLING, radar_episode_verdict(&b));
}

static void test_a_long_gap_starts_a_new_episode(void)
{
    radar_episode_t ep = {0};
    const uint8_t st[] = {S, S, S, S, S, S, S, S, S};
    const int32_t cm[] = {100, 100, 100, 100, 100, 100, 100, 100, 100};
    feed_samples(&ep, &RULES, 0, st, cm, 9); /* 4 s: dwelling */
    TEST_ASSERT_EQUAL(RADAR_VERDICT_DWELLING, radar_episode_verdict(&ep));
    for (uint32_t t = 5000; t <= 7000; t += 500) {
        radar_episode_update(&ep, &RULES, t, N, -1);
    }
    TEST_ASSERT_EQUAL(RADAR_VERDICT_NONE, radar_episode_verdict(&ep));
    radar_episode_update(&ep, &RULES, 7500, M, 200); /* a walker right after: a new episode, not dwelling */
    TEST_ASSERT_EQUAL(RADAR_VERDICT_PASSING, radar_episode_verdict(&ep));
}

static void test_a_silent_radar_ends_the_episode(void)
{
    radar_episode_t ep = {0};
    radar_episode_update(&ep, &RULES, 0, M, 200);
    radar_episode_update(&ep, &RULES, 500, M, 150);
    radar_episode_tick(&ep, &RULES, 1000);
    TEST_ASSERT_EQUAL(RADAR_VERDICT_PASSING, radar_episode_verdict(&ep));
    radar_episode_tick(&ep, &RULES, 6000); /* no packets for a long time */
    TEST_ASSERT_EQUAL(RADAR_VERDICT_NONE, radar_episode_verdict(&ep));
}

static rb_packet_t radar_packet(uint32_t node, uint32_t seq, uint32_t now, uint8_t state, int32_t cm)
{
    rb_packet_t p = presence_packet(node, seq, now, state != N);
    p.body.presence.moving_target = state == M;
    p.body.presence.stationary_target = state == S;
    p.body.presence.distance_m = cm >= 0 ? (float)cm / 100.0f : NAN;
    return p;
}

static void test_enforce_mode_keeps_a_passer_by_from_being_at_the_stove(void)
{
    for (int mode = RADAR_RULES_OBSERVE; mode <= RADAR_RULES_ENFORCE; mode++) {
        nodes_t n = {0};
        node_set_config_t cfg = NODES;
        cfg.require_all = true;
        cfg.rules = RULES;
        cfg.rules_mode = (radar_rules_mode_t)mode;
        node_set_init(&n.set, &cfg);
        uint32_t ev[NODE_SLOT_COUNT];
        node_inputs_t in;
        rb_packet_t t = thermal_packet(1, 0, 30.0f);
        node_set_on_packet(&n.set, &t, 0, NULL);
        uint32_t seq = 0, now = 0;
        /* both radars see a walker for 2.5 s */
        for (int i = 0; i < 5; i++, now += 500) {
            rb_packet_t a = radar_packet(NODE_A, ++seq, now, M, 300 - 40 * i), b = radar_packet(NODE_B, seq, now, M, 280 - 40 * i);
            node_set_on_packet(&n.set, &a, now, NULL);
            node_set_on_packet(&n.set, &b, now, NULL);
            rb_packet_t th = thermal_packet(seq + 1, now, 30.0f);
            node_set_on_packet(&n.set, &th, now, NULL);
            node_set_evaluate(&n.set, &HEALTH, now, ev);
        }
        node_set_inputs(&n.set, &in);
        TEST_ASSERT_EQUAL(RB_TRUE, in.presence_any);
        TEST_ASSERT_EQUAL(RADAR_VERDICT_PASSING, in.radar_verdict[0]);
        TEST_ASSERT_EQUAL(mode == RADAR_RULES_ENFORCE ? RB_FALSE : RB_TRUE, in.presence);
        /* then both settle at the stove for 5 s */
        for (int i = 0; i < 10; i++, now += 500) {
            rb_packet_t a = radar_packet(NODE_A, ++seq, now, S, 100), b = radar_packet(NODE_B, seq, now, S, 105);
            node_set_on_packet(&n.set, &a, now, NULL);
            node_set_on_packet(&n.set, &b, now, NULL);
            rb_packet_t th = thermal_packet(seq + 1, now, 30.0f);
            node_set_on_packet(&n.set, &th, now, NULL);
            node_set_evaluate(&n.set, &HEALTH, now, ev);
        }
        node_set_inputs(&n.set, &in);
        TEST_ASSERT_EQUAL(RADAR_VERDICT_DWELLING, in.radar_verdict[0]);
        TEST_ASSERT_EQUAL(RADAR_VERDICT_DWELLING, in.radar_verdict[1]);
        TEST_ASSERT_EQUAL(RB_TRUE, in.presence);
    }
}

static void test_rules_off_changes_nothing(void)
{
    nodes_t n = {0};
    node_set_config_t cfg = NODES;
    cfg.rules = RULES;
    cfg.rules_mode = RADAR_RULES_OFF;
    node_set_init(&n.set, &cfg);
    rb_packet_t a = radar_packet(NODE_A, 1, 0, M, 300), b = radar_packet(NODE_B, 1, 0, M, 300), t = thermal_packet(1, 0, 30.0f);
    node_set_on_packet(&n.set, &a, 0, NULL);
    node_set_on_packet(&n.set, &b, 0, NULL);
    node_set_on_packet(&n.set, &t, 0, NULL);
    uint32_t ev[NODE_SLOT_COUNT];
    node_set_evaluate(&n.set, &HEALTH, 0, ev);
    node_inputs_t in;
    node_set_inputs(&n.set, &in);
    TEST_ASSERT_EQUAL(RB_TRUE, in.presence);
    TEST_ASSERT_EQUAL(RADAR_VERDICT_NONE, in.radar_verdict[0]);
}

void run_sensor_node_tests(void)
{
    RUN_TEST(test_sequence_tracking);
    RUN_TEST(test_repeated_restarts_are_flagged);
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
    RUN_TEST(test_presence_fuse_all_truth_table);
    RUN_TEST(test_node_set_require_all_and_nearby);
    RUN_TEST(test_a_walker_is_passing_not_dwelling);
    RUN_TEST(test_a_slow_walker_who_never_settles_is_passing);
    RUN_TEST(test_a_cook_who_arrives_and_settles_is_dwelling);
    RUN_TEST(test_standing_still_behind_the_radar_looks_like_a_cook);
    RUN_TEST(test_the_cook_zone_limits_what_counts);
    RUN_TEST(test_a_long_gap_starts_a_new_episode);
    RUN_TEST(test_a_silent_radar_ends_the_episode);
    RUN_TEST(test_enforce_mode_keeps_a_passer_by_from_being_at_the_stove);
    RUN_TEST(test_rules_off_changes_nothing);
}
