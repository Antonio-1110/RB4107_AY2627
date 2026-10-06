#pragma once

/*
 * RB4107 MQTT topic tree (TODO section 20). Documented in docs/mqtt_topics.md.
 *
 *   <prefix>/controller/{status,heartbeat,state,faults}
 *   <prefix>/sensors/<node>/{presence,thermal,status,c4002_config,thermal_frame}
 *   <prefix>/sensors/<node>/c4002_set    (dashboard -> controller, subscribed)
 *   <prefix>/controller/command          (dashboard -> controller, subscribed)
 *   <prefix>/events/{warning,shutdown,fault}
 *
 * The prefix is configurable (RB_MQTT_TOPIC_PREFIX, default "rb4107").
 * Each topic has one fixed QoS/retain policy, defined here.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RB_TOPIC_CONTROLLER_STATUS = 0,   /* online/offline (retained, Last Will) */
    RB_TOPIC_CONTROLLER_HEARTBEAT,    /* liveness + uptime */
    RB_TOPIC_CONTROLLER_STATE,        /* full telemetry snapshot (~1 Hz and on every transition) */
    RB_TOPIC_CONTROLLER_FAULTS,       /* active fault list (retained, on change) */
    RB_TOPIC_SENSOR_PRESENCE,         /* per node */
    RB_TOPIC_SENSOR_THERMAL,          /* per node */
    RB_TOPIC_SENSOR_STATUS,           /* per node link state (retained, on change) */
    RB_TOPIC_SENSOR_C4002_CONFIG,     /* per presence node: C4002 settings / tuning result (retained) */
    RB_TOPIC_SENSOR_C4002_LIVE,       /* per presence node: raw C4002 result for the live radar view */
    RB_TOPIC_EVENT_WARNING,
    RB_TOPIC_EVENT_SHUTDOWN,
    RB_TOPIC_EVENT_FAULT,
    RB_TOPIC_SENSOR_THERMAL_FRAME,    /* per node: heat-map picture (display only) */
    RB_TOPIC_COUNT,
} rb_topic_t;

typedef struct {
    const char *suffix;      /* e.g. "controller/status"; "%s" is replaced by the node name */
    uint8_t qos;
    bool retain;
    bool per_node;
} rb_topic_info_t;

/* Last level of the command topic the controller subscribes to (remote C4002 tuning). */
#define RB_TOPIC_C4002_SET "c4002_set"

/* Dashboard -> controller command topic under the prefix (operator reset). */
#define RB_TOPIC_CONTROLLER_COMMAND "controller/command"

const rb_topic_info_t *rb_topic_info(rb_topic_t topic);

/* "node_01" for node ID 1. */
void rb_topic_node_name(uint32_t node_id, char *buf, size_t len);

/*
 * Build the full topic. node_id is ignored for controller/event topics.
 * Returns the length, or 0 if it doesn't fit.
 */
size_t rb_topic_build(rb_topic_t topic, const char *prefix, uint32_t node_id, char *buf, size_t len);

#ifdef __cplusplus
}
#endif
