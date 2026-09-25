#pragma once

/*
 * Controller-side state of one sensor node.
 *
 * Plain C with every timestamp passed in by the caller, so it can be tested
 * without hardware. The owner (the controller's safety task) serialises
 * access.
 */
#include <stdbool.h>
#include <stdint.h>
#include "rb_protocol.h"
#include "rb_sensor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* How a packet's sequence number relates to the previous one. */
typedef enum {
    NODE_SEQ_FIRST = 0,       /* first packet from this node: accepted */
    NODE_SEQ_OK,              /* next in sequence: accepted */
    NODE_SEQ_GAP,             /* newer, some packets missed: accepted */
    NODE_SEQ_NODE_RESTART,    /* node rebooted (uptime went backwards): accepted, tracking reset */
    NODE_SEQ_DUPLICATE,       /* same sequence again: dropped */
    NODE_SEQ_OUT_OF_ORDER,    /* older than the last accepted packet: dropped */
    NODE_SEQ_WRONG_NODE,      /* node ID doesn't match: dropped */
} node_seq_result_t;

typedef enum {
    NODE_LINK_NEVER_SEEN = 0,
    NODE_LINK_ONLINE,
    NODE_LINK_STALE,
    NODE_LINK_OFFLINE,
} node_link_state_t;

typedef struct {
    uint32_t node_id;

    /* Latest accepted SENSOR_DATA. */
    bool has_data;
    rb_sensor_data_t latest_data;
    uint32_t latest_data_rx_ms;

    /* Sensor fault flags reported by the node (HEARTBEAT / SENSOR_FAULT). */
    uint16_t node_fault_flags;

    /* Any accepted packet. */
    uint32_t last_received_ms;
    uint32_t last_sequence;
    uint32_t last_uptime_ms;
    bool online;              /* link == NODE_LINK_ONLINE */

    /* Counters. */
    uint32_t packets;
    uint32_t missed;
    uint32_t duplicates;
    uint32_t out_of_order;
    uint32_t restarts;
    uint32_t wrong_node;

    /* Health tracking (section 8). */
    node_link_state_t link;
    bool presence_ok;         /* last evaluated presence validity */
    bool thermal_ok;          /* last evaluated thermal validity */
} sensor_node_state_t;

void sensor_node_init(sensor_node_state_t *node, uint32_t node_id);

/* Apply one decoded packet received at now_ms. */
node_seq_result_t sensor_node_on_packet(sensor_node_state_t *node, const rb_packet_t *pkt, uint32_t now_ms);

const char *node_seq_result_name(node_seq_result_t result);
const char *node_link_state_name(node_link_state_t state);

#ifdef __cplusplus
}
#endif
