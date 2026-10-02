#pragma once

/*
 * The dashboard -> controller command for remote C4002 tuning, published by
 * Django on <prefix>/sensors/<node>/c4002_set. JSON contract in
 * docs/c4002_tuning.md and docs/schema/rb4107_mqtt.schema.json
 * ("c4002_command"). Example:
 *
 *   {"schema_version":2,"type":"c4002_command","request_id":7,"action":"apply",
 *    "range_max_cm":300,"presence_sensitivity":"low"}
 *
 * Only flat keys, integers, strings, booleans and integer arrays are used,
 * so a small hand-written parser is enough (ESP-IDF v6 no longer ships cJSON).
 */
#include <stdbool.h>
#include <stddef.h>
#include "rb_json.h"
#include "rb_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RB_C4002_CMD_DEFAULT_CALIB_DELAY_S 10u
#define RB_C4002_CMD_DEFAULT_CALIB_DURATION_S 30u

typedef struct {
    rb_c4002_config_msg_t msg;    /* target_node_id is left 0: it comes from the topic */
    char controller_id[40];       /* "" if the command didn't name one */
} rb_c4002_cmd_t;

/*
 * Parse and range-check one command. On failure returns false and writes a
 * short reason into err ("unknown key 'rang_max_cm'").
 */
bool rb_c4002_cmd_parse(const char *json, size_t len, rb_c4002_cmd_t *out, char *err, size_t err_len);

/* "rb4107/sensors/node_01/c4002_set" -> 1. Returns false for any other topic. */
bool rb_c4002_cmd_topic_node(const char *topic, const char *prefix, uint32_t *node_id);

/*
 * What happened to a command, published retained on
 * <prefix>/sensors/<node>/c4002_config ("c4002_config" in the schema).
 * The node's own results are ok / invalid / sensor_error; the controller adds
 * the ones for commands that never reached the node.
 */
typedef enum {
    RB_C4002_REPLY_NODE = 0,      /* result is the node's rb_c4002_result_t */
    RB_C4002_REPLY_REJECTED,      /* the command JSON was invalid (error says why) */
    RB_C4002_REPLY_UNKNOWN_NODE,  /* nothing heard from that node yet, so its MAC is unknown */
    RB_C4002_REPLY_SEND_FAILED,   /* ESP-NOW refused the packet */
    RB_C4002_REPLY_NO_REPLY,      /* sent, but the node never answered */
} rb_c4002_reply_kind_t;

typedef struct {
    rb_json_header_t hdr;
    const char *sensor_node;      /* "node_01" */
    rb_c4002_reply_kind_t kind;
    uint16_t request_id;
    uint8_t action;               /* rb_c4002_action_t, 0 if unknown (unparseable command) */
    const char *error;            /* REJECTED: the parser's reason; else NULL */
    bool has_ack;                 /* the fields below come from the node */
    rb_c4002_ack_t ack;
} rb_c4002_reply_t;

/* JSON for sensors/<node>/c4002_config. Returns the length, or 0 if it didn't fit. */
size_t rb_c4002_reply_json(const rb_c4002_reply_t *reply, char *buf, size_t len);

/*
 * One raw C4002 result from a node, published on <prefix>/sensors/<node>/c4002_live
 * ("c4002_live" in the schema) for the dashboard's live radar view.
 */
typedef struct {
    rb_json_header_t hdr;
    const char *sensor_node;      /* "node_01" */
    uint32_t node_uptime_ms;      /* node clock when it sent the result */
    rb_c4002_live_t live;
} rb_c4002_live_msg_t;

/* JSON for sensors/<node>/c4002_live. Returns the length, or 0 if it didn't fit. */
size_t rb_c4002_live_json(const rb_c4002_live_msg_t *msg, char *buf, size_t len);

/* Names used in the JSON for sensitivity groups: "low" / "mid" / "high" / "custom". */
const char *rb_c4002_sensitivity_name(uint8_t sensitivity);

#ifdef __cplusplus
}
#endif
