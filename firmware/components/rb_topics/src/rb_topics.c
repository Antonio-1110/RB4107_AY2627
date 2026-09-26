#include "rb_topics.h"

#include <stdio.h>

/*
 * QoS policy (TODO section 22): routine telemetry QoS 0 (a lost sample is
 * replaced a second later); state changes, faults and events QoS 1.
 */
static const rb_topic_info_t TOPICS[RB_TOPIC_COUNT] = {
    [RB_TOPIC_CONTROLLER_STATUS] = {"controller/status", 1, true, false},
    [RB_TOPIC_CONTROLLER_HEARTBEAT] = {"controller/heartbeat", 0, false, false},
    [RB_TOPIC_CONTROLLER_STATE] = {"controller/state", 0, true, false},
    [RB_TOPIC_CONTROLLER_FAULTS] = {"controller/faults", 1, true, false},
    [RB_TOPIC_SENSOR_PRESENCE] = {"sensors/%s/presence", 0, false, true},
    [RB_TOPIC_SENSOR_THERMAL] = {"sensors/%s/thermal", 0, false, true},
    [RB_TOPIC_SENSOR_STATUS] = {"sensors/%s/status", 1, true, true},
    [RB_TOPIC_EVENT_WARNING] = {"events/warning", 1, false, false},
    [RB_TOPIC_EVENT_SHUTDOWN] = {"events/shutdown", 1, false, false},
    [RB_TOPIC_EVENT_FAULT] = {"events/fault", 1, false, false},
};

const rb_topic_info_t *rb_topic_info(rb_topic_t topic)
{
    return (unsigned)topic < RB_TOPIC_COUNT ? &TOPICS[topic] : NULL;
}

void rb_topic_node_name(uint32_t node_id, char *buf, size_t len)
{
    snprintf(buf, len, "node_%02lu", (unsigned long)node_id);
}

size_t rb_topic_build(rb_topic_t topic, const char *prefix, uint32_t node_id, char *buf, size_t len)
{
    const rb_topic_info_t *info = rb_topic_info(topic);
    if (info == NULL || buf == NULL || len == 0) {
        return 0;
    }
    char suffix[48];
    if (info->per_node) {
        char node[16];
        rb_topic_node_name(node_id, node, sizeof(node));
        snprintf(suffix, sizeof(suffix), info->suffix, node);
    } else {
        snprintf(suffix, sizeof(suffix), "%s", info->suffix);
    }
    const int n = snprintf(buf, len, "%s/%s", prefix, suffix);
    return (n > 0 && (size_t)n < len) ? (size_t)n : 0;
}
