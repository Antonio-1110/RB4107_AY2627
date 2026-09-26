#include "fault_manager.h"

#include <stddef.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const fault_info_t FAULTS[FAULT_COUNT] = {
    [FAULT_PRESENCE_A_UNAVAILABLE] = {"presence_a_unavailable", FAULT_CLASS_SAFETY},
    [FAULT_PRESENCE_B_UNAVAILABLE] = {"presence_b_unavailable", FAULT_CLASS_SAFETY},
    [FAULT_THERMAL_UNAVAILABLE] = {"thermal_unavailable", FAULT_CLASS_SAFETY},
    [FAULT_PRESENCE_A_NODE_OFFLINE] = {"presence_a_node_offline", FAULT_CLASS_SAFETY},
    [FAULT_PRESENCE_B_NODE_OFFLINE] = {"presence_b_node_offline", FAULT_CLASS_SAFETY},
    [FAULT_THERMAL_NODE_OFFLINE] = {"thermal_node_offline", FAULT_CLASS_SAFETY},
    [FAULT_SHUTDOWN_OUTPUT] = {"shutdown_output_failure", FAULT_CLASS_SAFETY},
    [FAULT_SELF_TEST] = {"self_test_failed", FAULT_CLASS_SAFETY},
    [FAULT_ESPNOW_INVALID_PACKET] = {"espnow_invalid_packet", FAULT_CLASS_TELEMETRY},
    [FAULT_ESPNOW_LINK_DEGRADED] = {"espnow_link_degraded", FAULT_CLASS_TELEMETRY},
    [FAULT_ESPNOW_UNKNOWN_NODE] = {"espnow_unknown_node", FAULT_CLASS_TELEMETRY},
    [FAULT_RTC] = {"rtc_failure", FAULT_CLASS_TELEMETRY},
    [FAULT_NETWORK_DOWN] = {"network_disconnected", FAULT_CLASS_TELEMETRY},
    [FAULT_MQTT_DOWN] = {"mqtt_disconnected", FAULT_CLASS_TELEMETRY},
    [FAULT_RX_QUEUE_OVERFLOW] = {"rx_queue_overflow", FAULT_CLASS_TELEMETRY},
    [FAULT_EVENT_QUEUE_OVERFLOW] = {"event_queue_overflow", FAULT_CLASS_TELEMETRY},
};

static SemaphoreHandle_t s_lock;
static fault_status_t s_status[FAULT_COUNT];
static uint32_t (*s_now)(void);
static fault_change_cb_t s_cb;
static void *s_ctx;

void fault_manager_init(uint32_t (*now_fn)(void), fault_change_cb_t cb, void *ctx)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }
    memset(s_status, 0, sizeof(s_status));
    s_now = now_fn;
    s_cb = cb;
    s_ctx = ctx;
}

void fault_set(fault_id_t id, bool active, int32_t detail)
{
    if ((unsigned)id >= FAULT_COUNT || s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    fault_status_t *st = &s_status[id];
    const bool changed = st->active != active;
    if (changed) {
        st->active = active;
        st->since_ms = s_now != NULL ? s_now() : 0;
        if (active) {
            st->raise_count++;
        }
    }
    if (active) {
        st->detail = detail;
    }
    xSemaphoreGive(s_lock);

    if (changed && s_cb != NULL) {
        s_cb(id, active, detail, s_ctx);
    }
}

void fault_raise(fault_id_t id, int32_t detail)
{
    fault_set(id, true, detail);
}

void fault_clear(fault_id_t id)
{
    fault_set(id, false, 0);
}

bool fault_is_active(fault_id_t id)
{
    return (unsigned)id < FAULT_COUNT && (fault_active_mask() & (1u << id)) != 0;
}

uint32_t fault_active_mask(void)
{
    if (s_lock == NULL) {
        return 0;
    }
    uint32_t mask = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (unsigned i = 0; i < FAULT_COUNT; i++) {
        if (s_status[i].active) {
            mask |= 1u << i;
        }
    }
    xSemaphoreGive(s_lock);
    return mask;
}

bool fault_any_safety_active(void)
{
    const uint32_t mask = fault_active_mask();
    for (unsigned i = 0; i < FAULT_COUNT; i++) {
        if ((mask & (1u << i)) && FAULTS[i].fault_class == FAULT_CLASS_SAFETY) {
            return true;
        }
    }
    return false;
}

void fault_get_status(fault_id_t id, fault_status_t *out)
{
    memset(out, 0, sizeof(*out));
    if ((unsigned)id >= FAULT_COUNT || s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_status[id];
    xSemaphoreGive(s_lock);
}

const fault_info_t *fault_info(fault_id_t id)
{
    static const fault_info_t unknown = {"unknown", FAULT_CLASS_TELEMETRY};
    return (unsigned)id < FAULT_COUNT ? &FAULTS[id] : &unknown;
}

const char *fault_class_name(fault_class_t c)
{
    return c == FAULT_CLASS_SAFETY ? "SAFETY" : "TELEMETRY";
}
