#include "sensor_node.h"

#include <math.h>
#include <string.h>

#define NODE_RESTART_UPTIME_MARGIN_MS 1000u

void sensor_node_init(sensor_node_state_t *node, uint32_t node_id)
{
    memset(node, 0, sizeof(*node));
    node->node_id = node_id;
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
        node->wrong_node++;
        return NODE_SEQ_WRONG_NODE;
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
    case RB_MSG_SENSOR_DATA:
        node->latest_data = pkt->body.sensor;
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
    const bool presence_ok = data_fresh && node->latest_data.presence.valid;
    const bool thermal_ok = data_fresh && node->latest_data.thermal.valid;
    if (presence_ok != node->presence_ok) {
        events |= presence_ok ? NODE_EVT_PRESENCE_VALID : NODE_EVT_PRESENCE_INVALID;
        node->presence_ok = presence_ok;
    }
    if (thermal_ok != node->thermal_ok) {
        events |= thermal_ok ? NODE_EVT_THERMAL_VALID : NODE_EVT_THERMAL_INVALID;
        node->thermal_ok = thermal_ok;
    }
    return events;
}

void sensor_node_inputs(const sensor_node_state_t *node, node_inputs_t *out)
{
    memset(out, 0, sizeof(*out));
    out->link = node->link;
    out->node_fault_flags = node->node_fault_flags;

    const presence_reading_t *p = &node->latest_data.presence;
    out->presence = !node->presence_ok ? RB_UNKNOWN : (p->presence_detected ? RB_TRUE : RB_FALSE);

    const thermal_reading_t *t = &node->latest_data.thermal;
    out->thermal_valid = node->thermal_ok;
    out->hot_region_temp_c = node->thermal_ok ? t->hot_region_temp_c : NAN;
    out->max_temp_c = node->thermal_ok ? t->max_temp_c : NAN;
    out->temp_rate_c_per_min = node->thermal_ok ? t->temp_rate_c_per_min : NAN;
    out->pixels_above_threshold = node->thermal_ok ? t->pixels_above_threshold : 0;
}

const char *node_seq_result_name(node_seq_result_t result)
{
    static const char *const names[] = {"first", "ok", "gap", "node-restart", "duplicate", "out-of-order", "wrong-node"};
    return (unsigned)result < sizeof(names) / sizeof(names[0]) ? names[result] : "?";
}

const char *node_link_state_name(node_link_state_t state)
{
    static const char *const names[] = {"NEVER_SEEN", "ONLINE", "STALE", "OFFLINE"};
    return (unsigned)state < sizeof(names) / sizeof(names[0]) ? names[state] : "?";
}
