#include "sensor_node.h"

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
