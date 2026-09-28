#include "rb_telemetry.h"

#include <string.h>
#include "esp_log.h"
#include "fault_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rb_config.h"
#include "rb_controller.h"
#include "rb_json.h"
#include "rb_mqtt.h"
#include "rb_protocol.h"
#include "rb_time.h"
#include "rb_topics.h"
#include "rb_wallclock.h"

static const char *TAG = "MQTT";

#define TELEMETRY_STACK 6144
#define TOPIC_LEN 96
#define PAYLOAD_LEN 1536

static char s_payload[PAYLOAD_LEN];
static char s_timestamp[32];
static char s_node_names[NODE_SLOT_COUNT][16];
static uint32_t s_sequence;
static rb_telemetry_stats_t s_stats;

static void publish(rb_topic_t topic, uint32_t node_id, size_t len, int qos_override)
{
    if (len == 0) {
        ESP_LOGE(TAG, "payload for topic %d too large", topic);
        return;
    }
    char name[TOPIC_LEN];
    rb_topic_build(topic, CONFIG_RB_MQTT_TOPIC_PREFIX, node_id, name, sizeof(name));
    const rb_topic_info_t *info = rb_topic_info(topic);
    const int qos = qos_override >= 0 ? qos_override : info->qos;
    if (rb_mqtt_publish(name, s_payload, qos, info->retain) != ESP_OK) {
        s_stats.publish_failures++;
    }
}

static rb_json_header_t header(uint32_t mono_ms)
{
    rb_wallclock_iso8601(mono_ms, s_timestamp, sizeof(s_timestamp));
    return (rb_json_header_t){
        .controller_id = CONFIG_RB_MQTT_CONTROLLER_ID,
        .timestamp = s_timestamp[0] != '\0' ? s_timestamp : NULL,
        .uptime_ms = mono_ms,
        .sequence = ++s_sequence,
    };
}

/* Active faults as JSON entries. */
static size_t collect_faults(rb_json_fault_t *out, size_t max)
{
    const uint32_t mask = fault_active_mask();
    size_t n = 0;
    for (int id = 0; id < FAULT_COUNT && n < max; id++) {
        if (mask & (1u << id)) {
            const fault_info_t *info = fault_info(id);
            out[n++] = (rb_json_fault_t){info->name, fault_class_name(info->fault_class)};
        }
    }
    return n;
}

/* Fill the per-node part of t (sensors/<node>/... messages) for one slot. */
static void set_node(rb_telemetry_t *t, const rb_snapshot_t *snap, node_slot_t slot)
{
    const sensor_node_state_t *n = &snap->nodes.nodes[slot];
    t->sensor_node = s_node_names[slot];
    t->node.role = rb_node_role_name(n->role);
    t->node.link = node_link_state_name(n->link);
    t->node.valid = n->sensor_ok;
    t->node.missed = n->missed;
    t->node.restarts = n->restarts;
    t->node.fault_flags = n->node_fault_flags;

    const presence_reading_t *p = &n->presence;
    t->presence.valid = n->role == RB_NODE_ROLE_PRESENCE && n->sensor_ok;
    t->presence.detected = p->presence_detected;
    t->presence.moving = p->moving_target;
    t->presence.stationary = p->stationary_target;
    t->presence.distance_m = p->distance_m;
}

static void build_telemetry(rb_telemetry_t *t, const rb_snapshot_t *snap, rb_json_node_t *nodes,
                            rb_json_fault_t *faults, size_t max_faults)
{
    memset(t, 0, sizeof(*t));
    t->protocol_version = RB_PROTOCOL_VERSION;
    t->presence_state = rb_tristate_presence_name(snap->inputs.presence);

    size_t count = 0;
    for (int slot = 0; slot < NODE_SLOT_COUNT; slot++) {
        const sensor_node_state_t *n = &snap->nodes.nodes[slot];
        rb_topic_node_name(n->node_id, s_node_names[slot], sizeof(s_node_names[slot]));
        if (snap->nodes.enabled[slot]) {
            nodes[count++] = (rb_json_node_t){
                .name = s_node_names[slot],
                .role = rb_node_role_name(n->role),
                .link = node_link_state_name(n->link),
                .valid = n->sensor_ok,
                .detected = n->presence.presence_detected,
            };
        }
    }
    t->nodes = nodes;
    t->node_count = count;

    const sensor_node_state_t *th_node = &snap->nodes.nodes[NODE_SLOT_THERMAL];
    const thermal_reading_t *th = &th_node->thermal;
    t->thermal.valid = th_node->sensor_ok;
    t->thermal.max_c = th->max_temp_c;
    t->thermal.min_c = th->min_temp_c;
    t->thermal.mean_c = th->mean_temp_c;
    t->thermal.hot_region_c = th->hot_region_temp_c;
    t->thermal.rate_c_per_min = th->temp_rate_c_per_min;
    t->thermal.pixels_above_threshold = th->pixels_above_threshold;

    t->safety.state = safety_state_name(snap->state);
    t->safety.state_ms = snap->state_duration_ms;
    t->safety.unattended_ms = snap->unattended_ms;
    t->safety.buzzer = safety_buzzer_name(snap->outputs.buzzer);
    t->safety.shutdown = snap->outputs.shutdown;
    t->safety.test_timers = snap->test_timers;
    t->safety.loop_count = snap->loop_count;

    t->faults = faults;
    t->fault_count = collect_faults(faults, max_faults);
}

static void publish_periodic(void)
{
    rb_snapshot_t snap;
    rb_controller_get_snapshot(&snap);
    rb_json_node_t nodes[NODE_SLOT_COUNT];
    rb_json_fault_t faults[FAULT_COUNT];
    rb_telemetry_t t;
    build_telemetry(&t, &snap, nodes, faults, FAULT_COUNT);
    const uint32_t now = rb_time_mono_ms();

    t.hdr = header(now);
    publish(RB_TOPIC_CONTROLLER_STATE, 0, rb_json_telemetry(&t, s_payload, sizeof(s_payload)), -1);
    t.hdr = header(now);
    publish(RB_TOPIC_CONTROLLER_HEARTBEAT, 0, rb_json_heartbeat(&t, s_payload, sizeof(s_payload)), -1);
#if CONFIG_RB_MQTT_PUBLISH_SENSOR_TOPICS
    for (int slot = 0; slot < NODE_SLOT_COUNT; slot++) {
        if (!snap.nodes.enabled[slot]) {
            continue;
        }
        const sensor_node_state_t *n = &snap.nodes.nodes[slot];
        set_node(&t, &snap, (node_slot_t)slot);
        t.hdr = header(now);
        if (n->role == RB_NODE_ROLE_PRESENCE) {
            publish(RB_TOPIC_SENSOR_PRESENCE, n->node_id, rb_json_presence(&t, s_payload, sizeof(s_payload)), -1);
        } else {
            publish(RB_TOPIC_SENSOR_THERMAL, n->node_id, rb_json_thermal(&t, s_payload, sizeof(s_payload)), -1);
        }
    }
#endif
    s_stats.periodic++;
}

static void publish_event(const rb_event_t *evt)
{
    rb_snapshot_t snap;
    rb_controller_get_snapshot(&snap);
    rb_json_node_t nodes[NODE_SLOT_COUNT];
    rb_json_fault_t faults[FAULT_COUNT];
    rb_telemetry_t t;
    build_telemetry(&t, &snap, nodes, faults, FAULT_COUNT);

    rb_event_msg_t e = {
        .state = safety_state_name(snap.state),
        .unattended_ms = snap.unattended_ms,
    };
    switch (evt->type) {
    case RB_EVT_STATE_CHANGE: {
        /* Fresh state straight away (QoS 1: state changes matter). */
        t.hdr = header(evt->mono_ms);
        publish(RB_TOPIC_CONTROLLER_STATE, 0, rb_json_telemetry(&t, s_payload, sizeof(s_payload)), 1);
        e.state = safety_state_name(evt->state.to);
        e.from_state = safety_state_name(evt->state.from);
        e.reason = evt->state.reason;
        rb_topic_t topic = RB_TOPIC_COUNT;
        if (evt->state.to == SAFETY_WARNING) {
            e.event = "warning";
            topic = RB_TOPIC_EVENT_WARNING;
        } else if (evt->state.to == SAFETY_SHUTDOWN) {
            e.event = "shutdown";
            topic = RB_TOPIC_EVENT_SHUTDOWN;
        } else if (evt->state.to == SAFETY_FAULT) {
            e.event = "state_change";
            topic = RB_TOPIC_EVENT_FAULT;
        }
        if (topic != RB_TOPIC_COUNT) {
            e.hdr = header(evt->mono_ms);
            publish(topic, 0, rb_json_event(&e, s_payload, sizeof(s_payload)), -1);
        }
        break;
    }
    case RB_EVT_FAULT: {
        const fault_info_t *info = fault_info((fault_id_t)evt->fault.id);
        e.event = evt->fault.active ? "fault_raised" : "fault_cleared";
        e.fault = info->name;
        e.fault_class = fault_class_name(info->fault_class);
        e.hdr = header(evt->mono_ms);
        publish(RB_TOPIC_EVENT_FAULT, 0, rb_json_event(&e, s_payload, sizeof(s_payload)), -1);
        t.hdr = header(evt->mono_ms);
        publish(RB_TOPIC_CONTROLLER_FAULTS, 0, rb_json_faults(&t, s_payload, sizeof(s_payload)), -1);
        break;
    }
    case RB_EVT_NODE:
        if ((unsigned)evt->node.slot >= NODE_SLOT_COUNT) {
            break;
        }
        set_node(&t, &snap, evt->node.slot);
        t.hdr = header(evt->mono_ms);
        publish(RB_TOPIC_SENSOR_STATUS, evt->node.node_id, rb_json_node_status(&t, s_payload, sizeof(s_payload)), -1);
        break;
    }
    s_stats.events++;
}

static void telemetry_task(void *arg)
{
    uint32_t next_periodic = rb_time_mono_ms();
    for (;;) {
        const uint32_t now = rb_time_mono_ms();
        const int32_t until = (int32_t)(next_periodic - now);
        rb_event_t evt;
        if (rb_controller_next_event(&evt, until > 0 ? pdMS_TO_TICKS(until) : 0)) {
            publish_event(&evt);
        }
        if ((int32_t)(rb_time_mono_ms() - next_periodic) >= 0) {
            next_periodic += CONFIG_RB_MQTT_TELEMETRY_PERIOD_MS;
            /* After a long stall, skip the missed periods instead of bursting to catch up. */
            if ((int32_t)(rb_time_mono_ms() - next_periodic) > 0) {
                next_periodic = rb_time_mono_ms() + CONFIG_RB_MQTT_TELEMETRY_PERIOD_MS;
            }
            publish_periodic();
        }
    }
}

esp_err_t rb_telemetry_start(void)
{
    return xTaskCreate(telemetry_task, "telemetry", TELEMETRY_STACK, NULL, CONFIG_RB_CTRL_TELEMETRY_TASK_PRIO, NULL) ==
                   pdPASS
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

void rb_telemetry_get_stats(rb_telemetry_stats_t *out)
{
    *out = s_stats;
}
