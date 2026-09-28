#include "rb_app_faults.h"

#include "esp_log.h"
#include "fault_manager.h"
#include "rb_controller.h"
#include "rb_espnow.h"
#include "rb_outputs.h"
#include "rb_time.h"

static const char *TAG = "FAULT";

/* A counter-driven fault clears after this long without new increments. */
#define FAULT_QUIET_CLEAR_MS 10000

typedef struct {
    uint32_t last_value;
    uint32_t last_increase_ms;
} counter_watch_t;

static counter_watch_t s_invalid, s_foreign, s_overflow, s_dropped;

/* Per node slot (node_slot_t order). */
static const fault_id_t UNAVAILABLE[NODE_SLOT_COUNT] = {FAULT_PRESENCE_A_UNAVAILABLE, FAULT_PRESENCE_B_UNAVAILABLE,
                                                        FAULT_THERMAL_UNAVAILABLE};
static const fault_id_t OFFLINE[NODE_SLOT_COUNT] = {FAULT_PRESENCE_A_NODE_OFFLINE, FAULT_PRESENCE_B_NODE_OFFLINE,
                                                    FAULT_THERMAL_NODE_OFFLINE};
static uint32_t s_stale_slots;   /* bit per slot: node currently STALE */

static void on_fault_change(fault_id_t id, bool active, int32_t detail, void *ctx)
{
    const fault_info_t *info = fault_info(id);
    if (active) {
        ESP_LOGW(TAG, "%s fault RAISED: %s (detail %ld)", fault_class_name(info->fault_class), info->name, (long)detail);
    } else {
        ESP_LOGI(TAG, "%s fault cleared: %s", fault_class_name(info->fault_class), info->name);
    }
    const rb_event_t evt = {
        .type = RB_EVT_FAULT,
        .mono_ms = rb_time_mono_ms(),
        .fault = {.id = (uint16_t)id, .active = active},
    };
    rb_controller_post_event(&evt);
}

void rb_app_faults_init(const node_set_config_t *nodes)
{
    fault_manager_init(rb_time_mono_ms, on_fault_change, NULL);
    /* Nothing has been received yet: every configured node is genuinely unknown at boot. */
    for (int slot = 0; slot < NODE_SLOT_COUNT; slot++) {
        const bool configured = slot != NODE_SLOT_PRESENCE_B || nodes->presence_node_count > 1;
        if (configured) {
            fault_raise(OFFLINE[slot], -1);
            fault_raise(UNAVAILABLE[slot], -1);
        }
    }
}

void rb_app_faults_node_events(const sensor_node_state_t *node, node_slot_t slot, uint32_t events, void *ctx)
{
    if ((unsigned)slot >= NODE_SLOT_COUNT) {
        return;
    }
    if (events & NODE_EVT_ONLINE) {
        fault_clear(OFFLINE[slot]);
        s_stale_slots &= ~(1u << slot);
    }
    if (events & NODE_EVT_STALE) {
        s_stale_slots |= 1u << slot;
    }
    if (events & NODE_EVT_OFFLINE) {
        fault_raise(OFFLINE[slot], (int32_t)node->node_id);
        s_stale_slots &= ~(1u << slot); /* covered by the OFFLINE fault now */
    }
    /* One shared telemetry fault while any node is STALE (detail: the slot bits). */
    fault_set(FAULT_ESPNOW_LINK_DEGRADED, s_stale_slots != 0, (int32_t)s_stale_slots);

    if (events & NODE_EVT_SENSOR_INVALID) {
        fault_raise(UNAVAILABLE[slot], node->node_fault_flags);
    }
    if (events & NODE_EVT_SENSOR_VALID) {
        fault_clear(UNAVAILABLE[slot]);
    }
}

/* Raise while a counter keeps increasing, clear after it has been quiet for a while. */
static void watch_counter(counter_watch_t *w, uint32_t value, fault_id_t id, uint32_t now_ms)
{
    if (value != w->last_value) {
        w->last_value = value;
        w->last_increase_ms = now_ms;
        fault_raise(id, (int32_t)value);
    } else if (fault_is_active(id) && rb_time_elapsed(now_ms, w->last_increase_ms, FAULT_QUIET_CLEAR_MS)) {
        fault_clear(id);
    }
}

void rb_app_faults_tick(uint32_t now_ms, void *ctx)
{
    rb_espnow_rx_stats_t rx;
    rb_espnow_get_rx_stats(&rx);
    watch_counter(&s_invalid, rx.bad_length + rx.bad_magic + rx.bad_version + rx.bad_type + rx.bad_crc + rx.bad_role,
                  FAULT_ESPNOW_INVALID_PACKET, now_ms);
    watch_counter(&s_foreign, rb_controller_foreign_packets(), FAULT_ESPNOW_UNKNOWN_NODE, now_ms);
    watch_counter(&s_overflow, rx.queue_overflow, FAULT_RX_QUEUE_OVERFLOW, now_ms);
    watch_counter(&s_dropped, rb_controller_events_dropped(), FAULT_EVENT_QUEUE_OVERFLOW, now_ms);
    fault_set(FAULT_SHUTDOWN_OUTPUT, rb_outputs_fault(), 0);
}

bool rb_app_faults_safety_active(void *ctx)
{
    return fault_any_safety_active();
}

safety_selftest_t rb_app_self_test(void *ctx)
{
    const bool outputs_ok = !rb_outputs_fault();
    fault_set(FAULT_SELF_TEST, !outputs_ok, 0);
    return outputs_ok ? SAFETY_SELFTEST_PASS : SAFETY_SELFTEST_FAIL;
}
