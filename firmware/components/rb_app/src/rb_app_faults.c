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

static counter_watch_t s_invalid, s_overflow, s_dropped;

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

void rb_app_faults_init(void)
{
    fault_manager_init(rb_time_mono_ms, on_fault_change, NULL);
    /* Nothing has been received yet: these are genuinely unknown at boot. */
    fault_raise(FAULT_NODE_OFFLINE, -1);
    fault_raise(FAULT_C4002_UNAVAILABLE, -1);
    fault_raise(FAULT_MLX_UNAVAILABLE, -1);
}

void rb_app_faults_node_events(const sensor_node_state_t *node, uint32_t events, void *ctx)
{
    const int32_t flags = node->node_fault_flags;
    if (events & NODE_EVT_ONLINE) {
        fault_clear(FAULT_NODE_OFFLINE);
        fault_clear(FAULT_ESPNOW_LINK_DEGRADED);
    }
    if (events & NODE_EVT_STALE) {
        fault_raise(FAULT_ESPNOW_LINK_DEGRADED, (int32_t)node->missed);
    }
    if (events & NODE_EVT_OFFLINE) {
        fault_raise(FAULT_NODE_OFFLINE, 0);
    }
    if (events & NODE_EVT_PRESENCE_INVALID) {
        fault_raise(FAULT_C4002_UNAVAILABLE, flags);
    }
    if (events & NODE_EVT_PRESENCE_VALID) {
        fault_clear(FAULT_C4002_UNAVAILABLE);
    }
    if (events & NODE_EVT_THERMAL_INVALID) {
        fault_raise(FAULT_MLX_UNAVAILABLE, flags);
    }
    if (events & NODE_EVT_THERMAL_VALID) {
        fault_clear(FAULT_MLX_UNAVAILABLE);
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
    watch_counter(&s_invalid, rx.bad_length + rx.bad_magic + rx.bad_version + rx.bad_type + rx.bad_crc,
                  FAULT_ESPNOW_INVALID_PACKET, now_ms);
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
