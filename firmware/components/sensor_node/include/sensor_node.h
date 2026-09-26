#pragma once

/*
 * Controller-side state of the sensor nodes.
 *
 * The system has up to three ESP32-C6 nodes, each with one sensor:
 *
 *   presence A  (C4002)     ─┐
 *   presence B  (C4002)     ─┼─ ESP-NOW → controller
 *   thermal     (MLX90640)  ─┘
 *
 * sensor_node_state_t tracks one node (sequence numbers, link health,
 * validity of its reading). node_set_t holds all of them and combines them
 * into the inputs the safety state machine sees.
 *
 * Plain C with every timestamp passed in by the caller, so it can be tested
 * without hardware. The owner (the controller's safety task) serialises
 * access.
 */
#include <stdbool.h>
#include <stddef.h>
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
    NODE_SEQ_WRONG_NODE,      /* node ID not expected: dropped */
    NODE_SEQ_WRONG_ROLE,      /* expected node ID, but the wrong kind of node: dropped */
} node_seq_result_t;

typedef enum {
    NODE_LINK_NEVER_SEEN = 0,
    NODE_LINK_ONLINE,
    NODE_LINK_STALE,
    NODE_LINK_OFFLINE,
} node_link_state_t;

typedef struct {
    uint32_t node_id;
    rb_node_role_t role;

    /* Latest accepted data message (the one matching the role). */
    bool has_data;
    presence_reading_t presence;   /* presence nodes */
    thermal_reading_t thermal;     /* thermal nodes */
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
    uint32_t wrong_role;

    /* Health tracking (section 8). */
    node_link_state_t link;
    bool sensor_ok;           /* last evaluated: fresh, valid reading from an ONLINE node */
} sensor_node_state_t;

void sensor_node_init(sensor_node_state_t *node, uint32_t node_id, rb_node_role_t role);

/* Apply one decoded packet received at now_ms. */
node_seq_result_t sensor_node_on_packet(sensor_node_state_t *node, const rb_packet_t *pkt, uint32_t now_ms);

/* ---- Health monitoring (TODO section 8) ---- */

typedef struct {
    uint32_t stale_timeout_ms;    /* no packet for this long: ONLINE -> STALE */
    uint32_t offline_timeout_ms;  /* no packet for this long: -> OFFLINE */
} sensor_node_health_config_t;

/* Events returned by sensor_node_evaluate(), as a bitmask. */
typedef enum {
    NODE_EVT_ONLINE = 1u << 0,            /* entered ONLINE (first contact or recovery) */
    NODE_EVT_STALE = 1u << 1,
    NODE_EVT_OFFLINE = 1u << 2,
    NODE_EVT_SENSOR_VALID = 1u << 3,      /* the node's reading became usable (recovery) */
    NODE_EVT_SENSOR_INVALID = 1u << 4,    /* the node's reading became unusable (fault) */
} node_event_t;

/*
 * Re-evaluate link state and sensor validity at now_ms. Returns the
 * node_event_t bits for what changed since the previous call.
 */
uint32_t sensor_node_evaluate(sensor_node_state_t *node, const sensor_node_health_config_t *cfg, uint32_t now_ms);

/* This presence node's reading: UNKNOWN unless sensor_ok. */
rb_tristate_t sensor_node_presence(const sensor_node_state_t *node);

/* ---- The set of nodes ---- */

typedef enum {
    NODE_SLOT_PRESENCE_A = 0,
    NODE_SLOT_PRESENCE_B,
    NODE_SLOT_THERMAL,
    NODE_SLOT_COUNT,
} node_slot_t;

#define NODE_SET_MAX_PRESENCE 2

typedef struct {
    uint32_t presence_node_ids[NODE_SET_MAX_PRESENCE];
    uint8_t presence_node_count;  /* 1 (bench, one radar) or 2 */
    uint32_t thermal_node_id;
} node_set_config_t;

typedef struct {
    sensor_node_state_t nodes[NODE_SLOT_COUNT];
    bool enabled[NODE_SLOT_COUNT];
    uint32_t unknown_node;        /* packets from a node ID that isn't configured */
} node_set_t;

/*
 * Sensor inputs as the safety logic sees them. Anything not backed by
 * fresh, valid data from an ONLINE node is UNKNOWN or invalid, never
 * "no presence" or "cold".
 */
typedef struct {
    rb_tristate_t presence;                           /* combined, see presence_fuse() */
    rb_tristate_t presence_each[NODE_SET_MAX_PRESENCE];
    bool thermal_valid;
    float hot_region_temp_c;      /* NAN unless thermal_valid */
    float max_temp_c;
    float temp_rate_c_per_min;
    uint16_t pixels_above_threshold;
} node_inputs_t;

/* False if the configuration is unusable (count out of range, duplicate or zero IDs). */
bool node_set_config_valid(const node_set_config_t *cfg);

void node_set_init(node_set_t *set, const node_set_config_t *cfg);

/*
 * Route a packet to its node. *slot_out (optional) is set to the slot it went
 * to, or NODE_SLOT_COUNT if the node ID is not configured.
 */
node_seq_result_t node_set_on_packet(node_set_t *set, const rb_packet_t *pkt, uint32_t now_ms, node_slot_t *slot_out);

/* Evaluate every enabled node. events[slot] receives that node's node_event_t bits. */
void node_set_evaluate(node_set_t *set, const sensor_node_health_config_t *cfg, uint32_t now_ms,
                       uint32_t events[NODE_SLOT_COUNT]);

/* Combined inputs for the safety logic (uses the state from the last evaluate). */
void node_set_inputs(const node_set_t *set, node_inputs_t *out);

/*
 * Strict presence combination:
 *   - PRESENT if any sensor sees a person;
 *   - ABSENT only if every sensor gives a valid "absent";
 *   - UNKNOWN otherwise (one sensor missing and none seeing a person).
 * Missing data never turns into ABSENT.
 */
rb_tristate_t presence_fuse(const rb_tristate_t *readings, size_t count);

const char *node_slot_name(node_slot_t slot);   /* "presence_a", "presence_b", "thermal" */
const char *node_seq_result_name(node_seq_result_t result);
const char *node_link_state_name(node_link_state_t state);
const char *rb_tristate_presence_name(rb_tristate_t presence);   /* PRESENT / ABSENT / UNKNOWN */

#ifdef __cplusplus
}
#endif
