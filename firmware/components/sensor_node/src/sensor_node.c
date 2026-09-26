#include "sensor_node.h"

#include <math.h>
#include <string.h>

#define NODE_RESTART_UPTIME_MARGIN_MS 1000u

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
}

node_seq_result_t node_set_on_packet(node_set_t *set, const rb_packet_t *pkt, uint32_t now_ms, node_slot_t *slot_out)
{
    for (int slot = 0; slot < NODE_SLOT_COUNT; slot++) {
        if (set->enabled[slot] && set->nodes[slot].node_id == pkt->node_id) {
            if (slot_out != NULL) {
                *slot_out = (node_slot_t)slot;
            }
            return sensor_node_on_packet(&set->nodes[slot], pkt, now_ms);
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
    out->presence = presence_fuse(out->presence_each, n);

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
