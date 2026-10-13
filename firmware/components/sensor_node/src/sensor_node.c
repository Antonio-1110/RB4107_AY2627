#include "sensor_node.h"

#include <math.h>
#include <string.h>

#define NODE_RESTART_UPTIME_MARGIN_MS 1000u

/* ---- radar episodes (placeholder rules, see sensor_node.h) ---- */

#define CM_UNKNOWN 0xFFFFu

static bool in_zone(const radar_rules_config_t *cfg, uint16_t cm)
{
    return cfg->zone_max_cm == 0 || cm == CM_UNKNOWN || cm <= cfg->zone_max_cm;
}

static void episode_end(radar_episode_t *ep)
{
    ep->last_duration_ms = (uint32_t)(ep->last_present_ms - ep->start_ms);
    ep->last_stationary_ms = ep->stationary_ms;
    ep->last_span_cm = ep->max_cm >= ep->min_cm && ep->max_cm != 0 ? (uint16_t)(ep->max_cm - ep->min_cm) : 0;
    ep->last_verdict = ep->dwelling ? RADAR_VERDICT_DWELLING : RADAR_VERDICT_PASSING;
    ep->active = false;
    ep->dwelling = false;
}

void radar_episode_update(radar_episode_t *ep, const radar_rules_config_t *cfg, uint32_t now_ms, uint8_t state,
                          int32_t distance_cm)
{
    const bool present = state != 0;
    const uint16_t cm = distance_cm >= 0 && distance_cm < (int32_t)CM_UNKNOWN ? (uint16_t)distance_cm : CM_UNKNOWN;
    if (ep->active && (uint32_t)(now_ms - ep->last_sample_ms) > cfg->quiet_gap_ms * 4u) {
        episode_end(ep); /* samples stopped for a long time: whatever was there is over */
    }
    if (!ep->active) {
        if (!present) {
            return;
        }
        const radar_verdict_t last = ep->last_verdict;
        const uint32_t d = ep->last_duration_ms, s = ep->last_stationary_ms;
        const uint16_t span = ep->last_span_cm;
        memset(ep, 0, sizeof(*ep));
        ep->last_verdict = last;
        ep->last_duration_ms = d;
        ep->last_stationary_ms = s;
        ep->last_span_cm = span;
        ep->active = true;
        ep->start_ms = now_ms;
        ep->min_cm = CM_UNKNOWN;
    }
    if (ep->have_prev) {
        uint32_t dt = (uint32_t)(now_ms - ep->last_sample_ms);
        if (dt > cfg->quiet_gap_ms) {
            dt = cfg->quiet_gap_ms; /* a packet gap is not time spent present */
        }
        if (ep->prev_state != 0 && in_zone(cfg, ep->prev_cm)) {
            ep->zone_ms += dt;
            if (ep->prev_state == 2) {
                ep->stationary_ms += dt;
            } else {
                ep->moving_ms += dt;
            }
        }
    }
    ep->last_sample_ms = now_ms;
    ep->prev_state = state;
    ep->prev_cm = cm;
    ep->have_prev = true;
    if (present) {
        ep->last_present_ms = now_ms;
        if (cm != CM_UNKNOWN) {
            if (ep->min_cm == CM_UNKNOWN || cm < ep->min_cm) {
                ep->min_cm = cm;
            }
            if (cm > ep->max_cm) {
                ep->max_cm = cm;
            }
            ep->ring_ms[ep->ring_head] = now_ms;
            ep->ring_cm[ep->ring_head] = cm;
            ep->ring_head = (uint8_t)((ep->ring_head + 1) % RADAR_RING);
            if (ep->ring_count < RADAR_RING) {
                ep->ring_count++;
            }
            uint16_t lo = CM_UNKNOWN, hi = 0;
            for (uint8_t k = 0; k < ep->ring_count; k++) {
                const uint8_t idx = (uint8_t)((ep->ring_head + RADAR_RING - 1 - k) % RADAR_RING);
                if ((uint32_t)(now_ms - ep->ring_ms[idx]) > cfg->settle_window_ms) {
                    break;
                }
                lo = ep->ring_cm[idx] < lo ? ep->ring_cm[idx] : lo;
                hi = ep->ring_cm[idx] > hi ? ep->ring_cm[idx] : hi;
            }
            ep->recent_span_cm = hi >= lo && lo != CM_UNKNOWN ? (uint16_t)(hi - lo) : 0;
        }
    } else if ((uint32_t)(now_ms - ep->last_present_ms) >= cfg->quiet_gap_ms) {
        episode_end(ep);
        return;
    }
    if (!ep->dwelling && ep->zone_ms >= cfg->dwell_ms && ep->stationary_ms >= cfg->stationary_min_ms &&
        ep->recent_span_cm <= cfg->settle_span_cm) {
        ep->dwelling = true; /* arrived and settled: latched until the episode ends */
    }
}

void radar_episode_tick(radar_episode_t *ep, const radar_rules_config_t *cfg, uint32_t now_ms)
{
    if (ep->active && (uint32_t)(now_ms - ep->last_sample_ms) >= cfg->quiet_gap_ms * 2u + 1000u) {
        episode_end(ep); /* no packets: the node is gone or silent, the episode is over */
    }
}

radar_verdict_t radar_episode_verdict(const radar_episode_t *ep)
{
    if (!ep->active) {
        return RADAR_VERDICT_NONE;
    }
    return ep->dwelling ? RADAR_VERDICT_DWELLING : RADAR_VERDICT_PASSING;
}

const char *radar_verdict_name(radar_verdict_t verdict)
{
    static const char *const names[] = {"none", "passing", "dwelling"};
    return (unsigned)verdict < 3 ? names[verdict] : "?";
}

const char *radar_rules_mode_name(radar_rules_mode_t mode)
{
    static const char *const names[] = {"off", "observe", "enforce"};
    return (unsigned)mode < 3 ? names[mode] : "?";
}

void sensor_node_init(sensor_node_state_t *node, uint32_t node_id, rb_node_role_t role)
{
    memset(node, 0, sizeof(*node));
    node->node_id = node_id;
    node->role = role;
    node->link = NODE_LINK_NEVER_SEEN;
}

static node_seq_result_t classify(const sensor_node_state_t *node, const rb_packet_t *pkt)
{
    if (node->packets == 0) {
        return NODE_SEQ_FIRST;
    }
    /*
     * Uptime jumping backwards means the node rebooted and restarted its
     * sequence. The margin keeps a merely late packet (whose uptime is only
     * slightly older) classified as out-of-order.
     */
    if (pkt->uptime_ms + NODE_RESTART_UPTIME_MARGIN_MS < node->last_uptime_ms) {
        return NODE_SEQ_NODE_RESTART;
    }
    const int32_t delta = (int32_t)(pkt->sequence - node->last_sequence); /* wrap-safe */
    if (delta == 0) {
        return NODE_SEQ_DUPLICATE;
    }
    if (delta < 0) {
        return NODE_SEQ_OUT_OF_ORDER;
    }
    return delta == 1 ? NODE_SEQ_OK : NODE_SEQ_GAP;
}

node_seq_result_t sensor_node_on_packet(sensor_node_state_t *node, const rb_packet_t *pkt, uint32_t now_ms)
{
    if (pkt->node_id != node->node_id) {
        return NODE_SEQ_WRONG_NODE;
    }
    /* e.g. a thermal node flashed with a presence node's ID: never mix their data. */
    if (pkt->role != node->role) {
        node->wrong_role++;
        return NODE_SEQ_WRONG_ROLE;
    }
    const node_seq_result_t res = classify(node, pkt);
    switch (res) {
    case NODE_SEQ_DUPLICATE:
        node->duplicates++;
        return res;
    case NODE_SEQ_OUT_OF_ORDER:
        node->out_of_order++;
        return res;
    case NODE_SEQ_GAP:
        node->missed += pkt->sequence - node->last_sequence - 1u;
        break;
    case NODE_SEQ_NODE_RESTART:
        node->restart_ms[node->restarts % NODE_RESTART_HISTORY] = now_ms;
        node->restarts++;
        break;
    default:
        break;
    }

    node->packets++;
    node->last_sequence = pkt->sequence;
    node->last_uptime_ms = pkt->uptime_ms;
    node->last_received_ms = now_ms;

    switch (pkt->type) {
    case RB_MSG_PRESENCE_DATA:
        node->presence = pkt->body.presence;
        node->latest_data_rx_ms = now_ms;
        node->has_data = true;
        break;
    case RB_MSG_THERMAL_DATA:
        node->thermal = pkt->body.thermal;
        node->latest_data_rx_ms = now_ms;
        node->has_data = true;
        break;
    case RB_MSG_HEARTBEAT:
        node->node_fault_flags = pkt->body.heartbeat.fault_flags;
        break;
    case RB_MSG_SENSOR_FAULT:
        node->node_fault_flags = pkt->body.fault.fault_flags;
        break;
    default: /* C4002_CONFIG_ACK, C4002_LIVE: only prove the node is alive; the application handles them.
                THERMAL_FRAME never gets here: the controller handles it outside the safety task.
                Valve messages never come from a sensor node (the decoder checks the role). */
        break;
    }
    return res;
}

static node_link_state_t link_state(const sensor_node_state_t *node, const sensor_node_health_config_t *cfg,
                                    uint32_t now_ms)
{
    if (node->packets == 0) {
        return NODE_LINK_NEVER_SEEN;
    }
    const uint32_t age = now_ms - node->last_received_ms;
    if (age >= cfg->offline_timeout_ms) {
        return NODE_LINK_OFFLINE;
    }
    if (age >= cfg->stale_timeout_ms) {
        return NODE_LINK_STALE;
    }
    return NODE_LINK_ONLINE;
}

/* Restarts recorded within the last window_ms. */
static uint32_t recent_restarts(const sensor_node_state_t *node, uint32_t now_ms, uint32_t window_ms)
{
    const uint32_t kept = node->restarts < NODE_RESTART_HISTORY ? node->restarts : NODE_RESTART_HISTORY;
    uint32_t count = 0;
    for (uint32_t i = 0; i < kept; i++) {
        if ((uint32_t)(now_ms - node->restart_ms[i]) < window_ms) {
            count++;
        }
    }
    return count;
}

uint32_t sensor_node_evaluate(sensor_node_state_t *node, const sensor_node_health_config_t *cfg, uint32_t now_ms)
{
    uint32_t events = 0;

    const node_link_state_t link = link_state(node, cfg, now_ms);
    if (link != node->link) {
        events |= link == NODE_LINK_ONLINE ? NODE_EVT_ONLINE : link == NODE_LINK_STALE ? NODE_EVT_STALE : NODE_EVT_OFFLINE;
        node->link = link;
    }
    node->online = link == NODE_LINK_ONLINE;

    /* A reading only counts if the node is ONLINE and the data itself is fresh. */
    const bool data_fresh = node->online && node->has_data && (now_ms - node->latest_data_rx_ms) < cfg->stale_timeout_ms;
    const bool valid = node->role == RB_NODE_ROLE_PRESENCE ? node->presence.valid : node->thermal.valid;
    const bool sensor_ok = data_fresh && valid;
    if (sensor_ok != node->sensor_ok) {
        events |= sensor_ok ? NODE_EVT_SENSOR_VALID : NODE_EVT_SENSOR_INVALID;
        node->sensor_ok = sensor_ok;
    }

    const uint32_t limit = cfg->restart_limit < NODE_RESTART_HISTORY ? cfg->restart_limit : NODE_RESTART_HISTORY;
    const bool restarting = limit > 0 && recent_restarts(node, now_ms, cfg->restart_window_ms) >= limit;
    if (restarting != node->restarting) {
        events |= restarting ? NODE_EVT_RESTARTING : NODE_EVT_RESTARTS_SETTLED;
        node->restarting = restarting;
    }
    return events;
}

rb_tristate_t sensor_node_presence(const sensor_node_state_t *node)
{
    if (node->role != RB_NODE_ROLE_PRESENCE || !node->sensor_ok) {
        return RB_UNKNOWN;
    }
    return node->presence.presence_detected ? RB_TRUE : RB_FALSE;
}

/* ---- The set of nodes ---- */

bool node_set_config_valid(const node_set_config_t *cfg)
{
    if (cfg->presence_node_count < 1 || cfg->presence_node_count > NODE_SET_MAX_PRESENCE || cfg->thermal_node_id == 0) {
        return false;
    }
    for (int i = 0; i < cfg->presence_node_count; i++) {
        const uint32_t id = cfg->presence_node_ids[i];
        if (id == 0 || id == cfg->thermal_node_id) {
            return false;
        }
        for (int j = 0; j < i; j++) {
            if (id == cfg->presence_node_ids[j]) {
                return false;
            }
        }
    }
    return true;
}

void node_set_init(node_set_t *set, const node_set_config_t *cfg)
{
    memset(set, 0, sizeof(*set));
    for (int i = 0; i < NODE_SET_MAX_PRESENCE; i++) {
        const node_slot_t slot = (node_slot_t)(NODE_SLOT_PRESENCE_A + i);
        sensor_node_init(&set->nodes[slot], cfg->presence_node_ids[i], RB_NODE_ROLE_PRESENCE);
        set->enabled[slot] = i < cfg->presence_node_count;
    }
    sensor_node_init(&set->nodes[NODE_SLOT_THERMAL], cfg->thermal_node_id, RB_NODE_ROLE_THERMAL);
    set->enabled[NODE_SLOT_THERMAL] = true;
    set->require_all = cfg->require_all;
    set->rules = cfg->rules;
    set->rules_mode = cfg->rules_mode;
}

node_seq_result_t node_set_on_packet(node_set_t *set, const rb_packet_t *pkt, uint32_t now_ms, node_slot_t *slot_out)
{
    for (int slot = 0; slot < NODE_SLOT_COUNT; slot++) {
        if (set->enabled[slot] && set->nodes[slot].node_id == pkt->node_id) {
            if (slot_out != NULL) {
                *slot_out = (node_slot_t)slot;
            }
            const node_seq_result_t res = sensor_node_on_packet(&set->nodes[slot], pkt, now_ms);
            const bool accepted = res == NODE_SEQ_FIRST || res == NODE_SEQ_OK || res == NODE_SEQ_GAP || res == NODE_SEQ_NODE_RESTART;
            if (accepted && set->rules_mode != RADAR_RULES_OFF && pkt->type == RB_MSG_PRESENCE_DATA &&
                pkt->body.presence.valid) {
                const presence_reading_t *p = &pkt->body.presence;
                const uint8_t state = p->moving_target ? 1 : (p->stationary_target || p->presence_detected ? 2 : 0);
                radar_episode_update(&set->nodes[slot].episode, &set->rules, now_ms, state,
                                     isfinite(p->distance_m) ? (int32_t)lroundf(p->distance_m * 100.0f) : -1);
            }
            return res;
        }
    }
    if (slot_out != NULL) {
        *slot_out = NODE_SLOT_COUNT;
    }
    set->unknown_node++;
    return NODE_SEQ_WRONG_NODE;
}

void node_set_evaluate(node_set_t *set, const sensor_node_health_config_t *cfg, uint32_t now_ms,
                       uint32_t events[NODE_SLOT_COUNT])
{
    for (int slot = 0; slot < NODE_SLOT_COUNT; slot++) {
        events[slot] = set->enabled[slot] ? sensor_node_evaluate(&set->nodes[slot], cfg, now_ms) : 0;
        if (set->enabled[slot] && set->rules_mode != RADAR_RULES_OFF && set->nodes[slot].role == RB_NODE_ROLE_PRESENCE) {
            radar_episode_tick(&set->nodes[slot].episode, &set->rules, now_ms);
        }
    }
}

rb_tristate_t presence_fuse(const rb_tristate_t *readings, size_t count)
{
    bool all_absent = count > 0;
    for (size_t i = 0; i < count; i++) {
        if (readings[i] == RB_TRUE) {
            return RB_TRUE;
        }
        all_absent &= readings[i] == RB_FALSE;
    }
    return all_absent ? RB_FALSE : RB_UNKNOWN;
}

rb_tristate_t presence_fuse_all(const rb_tristate_t *readings, size_t count)
{
    size_t working = 0;
    for (size_t i = 0; i < count; i++) {
        if (readings[i] == RB_FALSE) {
            return RB_FALSE;
        }
        working += readings[i] == RB_TRUE;
    }
    return working > 0 ? RB_TRUE : RB_UNKNOWN;
}

void node_set_inputs(const node_set_t *set, node_inputs_t *out)
{
    memset(out, 0, sizeof(*out));
    size_t n = 0;
    for (int i = 0; i < NODE_SET_MAX_PRESENCE; i++) {
        const node_slot_t slot = (node_slot_t)(NODE_SLOT_PRESENCE_A + i);
        out->presence_each[i] = sensor_node_presence(&set->nodes[slot]);
        if (set->enabled[slot]) {
            n++;
        }
    }
    /* Enabled presence slots are always the first n. */
    out->presence_any = presence_fuse(out->presence_each, n);
    /* ENFORCE: a radar that sees someone who is only passing does not count as "at the stove". */
    rb_tristate_t at_stove[NODE_SET_MAX_PRESENCE];
    for (int i = 0; i < NODE_SET_MAX_PRESENCE; i++) {
        const node_slot_t slot = (node_slot_t)(NODE_SLOT_PRESENCE_A + i);
        out->radar_verdict[i] = set->rules_mode != RADAR_RULES_OFF ? radar_episode_verdict(&set->nodes[slot].episode) : RADAR_VERDICT_NONE;
        at_stove[i] = out->presence_each[i];
        if (set->rules_mode == RADAR_RULES_ENFORCE && at_stove[i] == RB_TRUE && out->radar_verdict[i] != RADAR_VERDICT_DWELLING) {
            at_stove[i] = RB_FALSE;
        }
    }
    out->presence = set->require_all ? presence_fuse_all(at_stove, n) : presence_fuse(at_stove, n);

    const sensor_node_state_t *th = &set->nodes[NODE_SLOT_THERMAL];
    const thermal_reading_t *t = &th->thermal;
    out->thermal_valid = th->sensor_ok;
    out->hot_region_temp_c = th->sensor_ok ? t->hot_region_temp_c : NAN;
    out->max_temp_c = th->sensor_ok ? t->max_temp_c : NAN;
    out->temp_rate_c_per_min = th->sensor_ok ? t->temp_rate_c_per_min : NAN;
    out->pixels_above_threshold = th->sensor_ok ? t->pixels_above_threshold : 0;
}

const char *node_slot_name(node_slot_t slot)
{
    static const char *const names[] = {"presence_a", "presence_b", "thermal"};
    return (unsigned)slot < sizeof(names) / sizeof(names[0]) ? names[slot] : "?";
}

const char *node_seq_result_name(node_seq_result_t result)
{
    static const char *const names[] = {"first",        "ok",           "gap",        "node-restart",
                                        "duplicate",    "out-of-order", "wrong-node", "wrong-role"};
    return (unsigned)result < sizeof(names) / sizeof(names[0]) ? names[result] : "?";
}

const char *node_link_state_name(node_link_state_t state)
{
    static const char *const names[] = {"NEVER_SEEN", "ONLINE", "STALE", "OFFLINE"};
    return (unsigned)state < sizeof(names) / sizeof(names[0]) ? names[state] : "?";
}

const char *rb_tristate_presence_name(rb_tristate_t presence)
{
    return presence == RB_TRUE ? "PRESENT" : presence == RB_FALSE ? "ABSENT" : "UNKNOWN";
}
