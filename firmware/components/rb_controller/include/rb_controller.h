#pragma once

/*
 * Controller runtime (TODO section 12).
 *
 *   ESP-NOW callback -> rx queue -> safety task -> state machine -> outputs hook
 *                                        |
 *                                        +-> event queue -> telemetry task (MQTT, lower priority)
 *                                        +-> snapshot (mutex) -> diagnostics / telemetry
 *
 * The safety task owns the sensor-node state and the state machine; nothing
 * else touches them. It never blocks on anything the telemetry side owns:
 * events are posted with a zero timeout and the snapshot lock is only held
 * for a struct copy. An MQTT stall therefore can't stop safety processing.
 */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "rb_protocol.h"
#include "safety.h"
#include "sensor_node.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RB_EVT_STATE_CHANGE = 0,  /* safety transition */
    RB_EVT_NODE,              /* sensor-node health events (node_event_t bits) */
    RB_EVT_FAULT,             /* fault raised/cleared (fault manager, section 16) */
} rb_event_type_t;

typedef struct {
    rb_event_type_t type;
    uint32_t mono_ms;         /* monotonic time of the event */
    union {
        struct {
            safety_state_t from;
            safety_state_t to;
            const char *reason;   /* string literal owned by the safety component */
        } state;
        struct {
            uint32_t node_id;
            uint32_t events;
        } node;
        struct {
            uint16_t id;
            bool active;
        } fault;
    };
} rb_event_t;

typedef struct {
    uint32_t mono_ms;
    safety_state_t state;
    uint32_t state_duration_ms;
    uint32_t unattended_ms;
    safety_outputs_t outputs;
    const char *last_reason;
    uint32_t transitions;
    sensor_node_state_t node;
    node_inputs_t inputs;
    uint32_t loop_count;          /* safety loop iterations: proves the task is alive */
    uint32_t last_loop_ms;
    uint32_t events_dropped;
    bool test_timers;             /* accelerated diagnostic timers active */
} rb_snapshot_t;

/* Hooks the application provides. All are optional and all run in the safety task. */
typedef struct {
    /* Drive the buzzer and relay. Must not block. */
    void (*apply_outputs)(const safety_outputs_t *outputs, void *ctx);
    /* Polled while in SELF_TEST. NULL = pass straight away. */
    safety_selftest_t (*self_test)(void *ctx);
    /* Any safety-relevant fault active (fault manager)? */
    bool (*safety_fault_active)(void *ctx);
    /* Told about node health events (fault manager raises/clears faults here). */
    void (*node_events)(const sensor_node_state_t *node, uint32_t events, void *ctx);
    /* Last chance to change the inputs (diagnostic simulation, section 27). */
    void (*override_inputs)(safety_inputs_t *inputs, void *ctx);
    void *ctx;
} rb_controller_hooks_t;

typedef struct {
    safety_config_t safety;
    sensor_node_health_config_t health;
    uint32_t node_id;
    uint32_t rx_queue_len;
    uint32_t event_queue_len;
    uint32_t tick_ms;
    uint32_t safety_task_priority;
} rb_controller_config_t;

rb_controller_config_t rb_controller_config_from_kconfig(void);

esp_err_t rb_controller_start(const rb_controller_config_t *config, const rb_controller_hooks_t *hooks);

/* Queue the ESP-NOW receiver posts to (item type rb_espnow_rx_t). */
QueueHandle_t rb_controller_rx_queue(void);

/* Inject a packet as if it came over ESP-NOW (simulation). Non-blocking. */
bool rb_controller_inject(const rb_packet_t *packet);

/* Post an event from another task (e.g. the fault manager). Non-blocking. */
bool rb_controller_post_event(const rb_event_t *event);

/* Next event for the telemetry side. */
bool rb_controller_next_event(rb_event_t *out, TickType_t wait);

void rb_controller_get_snapshot(rb_snapshot_t *out);

/* Operator reset / acknowledge (button, console). Consumed by the safety task. */
void rb_controller_request_reset(void);

/*
 * Swap the safety configuration (e.g. accelerated test timers). Applied by
 * the safety task at its next step. The logic is the same; only the numbers
 * change.
 */
esp_err_t rb_controller_set_safety_config(const safety_config_t *config, bool test_timers);

#ifdef __cplusplus
}
#endif
