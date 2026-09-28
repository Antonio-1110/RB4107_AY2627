#include "rb_controller.h"

#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "rb_config.h"
#include "rb_log.h"
#include "rb_espnow.h"
#include "rb_safety_config.h"
#include "rb_time.h"

static const char *TAG = "SAFETY";

#define SAFETY_TASK_STACK 6144

static rb_controller_config_t s_cfg;
static rb_controller_hooks_t s_hooks;
static QueueHandle_t s_rx_queue;
static QueueHandle_t s_event_queue;
static SemaphoreHandle_t s_snapshot_lock;
static rb_snapshot_t s_snapshot;
static uint32_t s_events_dropped;
static uint32_t s_foreign_packets;   /* written and read by the safety task only */

/* Configuration changes and reset requests from other tasks. */
static portMUX_TYPE s_req_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_reset_requested;
static bool s_config_pending;
static safety_config_t s_pending_config;
static bool s_pending_test_timers;

rb_controller_config_t rb_controller_config_from_kconfig(void)
{
    return (rb_controller_config_t){
        .safety = rb_safety_config_from_kconfig(),
        .health = {
            .stale_timeout_ms = CONFIG_RB_CTRL_NODE_STALE_MS,
            .offline_timeout_ms = CONFIG_RB_CTRL_NODE_OFFLINE_MS,
        },
        .nodes = {
            .presence_node_ids = {CONFIG_RB_CTRL_PRESENCE_A_NODE_ID, CONFIG_RB_CTRL_PRESENCE_B_NODE_ID},
            .presence_node_count = CONFIG_RB_CTRL_PRESENCE_NODE_COUNT,
            .thermal_node_id = CONFIG_RB_CTRL_THERMAL_NODE_ID,
        },
        .rx_queue_len = CONFIG_RB_CTRL_RX_QUEUE_LEN,
        .event_queue_len = CONFIG_RB_CTRL_EVENT_QUEUE_LEN,
        .tick_ms = CONFIG_RB_CTRL_SAFETY_TICK_MS,
        .safety_task_priority = CONFIG_RB_CTRL_SAFETY_TASK_PRIO,
    };
}

bool rb_controller_post_event(const rb_event_t *event)
{
    if (s_event_queue == NULL || xQueueSend(s_event_queue, event, 0) != pdTRUE) {
        portENTER_CRITICAL(&s_req_lock);
        s_events_dropped++;
        portEXIT_CRITICAL(&s_req_lock);
        RB_LOG_EVERY_MS(5000, ESP_LOGW, TAG, "event queue full: telemetry is behind, events dropped");
        return false;
    }
    return true;
}

static void on_transition(safety_state_t from, safety_state_t to, const char *reason, uint32_t now_ms, void *ctx)
{
    (void)ctx;
    if (to == SAFETY_SHUTDOWN || to == SAFETY_FAULT || to == SAFETY_WARNING) {
        ESP_LOGW(TAG, "%s -> %s (%s)", safety_state_name(from), safety_state_name(to), reason);
    } else {
        ESP_LOGI(TAG, "%s -> %s (%s)", safety_state_name(from), safety_state_name(to), reason);
    }
    const rb_event_t evt = {
        .type = RB_EVT_STATE_CHANGE,
        .mono_ms = now_ms,
        .state = {.from = from, .to = to, .reason = reason},
    };
    rb_controller_post_event(&evt);
}

static void log_node_events(const sensor_node_state_t *node, node_slot_t slot, uint32_t events)
{
    static const char *const NAMES[] = {"ONLINE", "STALE", "OFFLINE", "sensor reading restored",
                                        "sensor reading unavailable"};
    for (unsigned bit = 0; bit < sizeof(NAMES) / sizeof(NAMES[0]); bit++) {
        if (events & (1u << bit)) {
            const bool bad = (1u << bit) & (NODE_EVT_STALE | NODE_EVT_OFFLINE | NODE_EVT_SENSOR_INVALID);
            if (bad) {
                ESP_LOGW("SENSOR", "%s node_%02lu %s", node_slot_name(slot), (unsigned long)node->node_id, NAMES[bit]);
            } else {
                ESP_LOGI("SENSOR", "%s node_%02lu %s", node_slot_name(slot), (unsigned long)node->node_id, NAMES[bit]);
            }
        }
    }
}

static void on_packet(node_set_t *nodes, const rb_espnow_rx_t *rx)
{
    node_slot_t slot;
    const node_seq_result_t res = node_set_on_packet(nodes, &rx->packet, rx->rx_ms, &slot);
    if (res == NODE_SEQ_WRONG_NODE || res == NODE_SEQ_WRONG_ROLE) {
        s_foreign_packets++;
        RB_LOG_EVERY_MS(5000, ESP_LOGW, "SENSOR", "dropped packet from node_%02lu (%s node): %s",
                        (unsigned long)rx->packet.node_id, rb_node_role_name(rx->packet.role),
                        res == NODE_SEQ_WRONG_NODE ? "node ID not configured on the controller"
                                                   : "configured for the other role (check RB_NODE_ID)");
    }
}

static void safety_task(void *arg)
{
    static safety_sm_t sm;
    static node_set_t nodes;
    node_set_init(&nodes, &s_cfg.nodes);
    safety_init(&sm, &s_cfg.safety, on_transition, NULL, rb_time_mono_ms());
    bool test_timers = false;
    uint32_t loops = 0;

    for (;;) {
        /* Sensor data wakes the task immediately; otherwise it runs once per tick. */
        rb_espnow_rx_t rx;
        if (xQueueReceive(s_rx_queue, &rx, pdMS_TO_TICKS(s_cfg.tick_ms)) == pdTRUE) {
            do {
                on_packet(&nodes, &rx);
            } while (xQueueReceive(s_rx_queue, &rx, 0) == pdTRUE);
        }
        const uint32_t now = rb_time_mono_ms();

        /* Requests from other tasks. */
        portENTER_CRITICAL(&s_req_lock);
        const bool reset = s_reset_requested;
        s_reset_requested = false;
        const bool new_config = s_config_pending;
        s_config_pending = false;
        const safety_config_t pending = s_pending_config;
        const bool pending_test = s_pending_test_timers;
        portEXIT_CRITICAL(&s_req_lock);
        if (new_config && safety_set_config(&sm, &pending)) {
            test_timers = pending_test;
            ESP_LOGW(TAG, "safety timers now %s: warning %lu ms, shutdown %lu ms", test_timers ? "TEST" : "normal",
                     (unsigned long)pending.warning_timeout_ms, (unsigned long)pending.shutdown_timeout_ms);
        }

        if (s_hooks.tick != NULL) {
            s_hooks.tick(now, s_hooks.ctx);
        }

        /* Sensor-node health, one node at a time. */
        uint32_t events[NODE_SLOT_COUNT];
        node_set_evaluate(&nodes, &s_cfg.health, now, events);
        for (int slot = 0; slot < NODE_SLOT_COUNT; slot++) {
            if (events[slot] == 0) {
                continue;
            }
            const sensor_node_state_t *node = &nodes.nodes[slot];
            log_node_events(node, (node_slot_t)slot, events[slot]);
            const rb_event_t evt = {
                .type = RB_EVT_NODE,
                .mono_ms = now,
                .node = {.slot = (node_slot_t)slot, .node_id = node->node_id, .events = events[slot]},
            };
            rb_controller_post_event(&evt);
            if (s_hooks.node_events != NULL) {
                s_hooks.node_events(node, (node_slot_t)slot, events[slot], s_hooks.ctx);
            }
        }

        /* Safety inputs -> state machine -> outputs. */
        node_inputs_t ni;
        node_set_inputs(&nodes, &ni);
        safety_inputs_t in = {
            .presence = ni.presence,
            .thermal_valid = ni.thermal_valid,
            .hot_region_temp_c = ni.hot_region_temp_c,
            .temp_rate_c_per_min = ni.temp_rate_c_per_min,
            .self_test = s_hooks.self_test != NULL ? s_hooks.self_test(s_hooks.ctx) : SAFETY_SELFTEST_PASS,
            .reset_request = reset,
            .safety_fault = s_hooks.safety_fault_active != NULL && s_hooks.safety_fault_active(s_hooks.ctx),
        };
        if (s_hooks.override_inputs != NULL) {
            s_hooks.override_inputs(&in, s_hooks.ctx);
        }
        const safety_outputs_t *out = safety_step(&sm, &in, now);
        if (s_hooks.apply_outputs != NULL) {
            s_hooks.apply_outputs(out, s_hooks.ctx);
        }

        /* Publish the snapshot (short copy, never blocks for long). */
        loops++;
        if (xSemaphoreTake(s_snapshot_lock, pdMS_TO_TICKS(5)) == pdTRUE) {
            s_snapshot.mono_ms = now;
            s_snapshot.state = sm.state;
            s_snapshot.state_duration_ms = now - sm.state_entered_ms;
            s_snapshot.unattended_ms = safety_unattended_ms(&sm, now);
            s_snapshot.outputs = *out;
            s_snapshot.last_reason = sm.last_reason;
            s_snapshot.transitions = sm.transitions;
            s_snapshot.nodes = nodes;
            s_snapshot.inputs = ni;
            s_snapshot.inputs.presence = in.presence; /* show what the state machine actually used */
            s_snapshot.loop_count = loops;
            s_snapshot.last_loop_ms = now;
            s_snapshot.events_dropped = s_events_dropped;
            s_snapshot.test_timers = test_timers;
            xSemaphoreGive(s_snapshot_lock);
        }
    }
}

esp_err_t rb_controller_init(const rb_controller_config_t *config)
{
    if (s_rx_queue != NULL) {
        return ESP_OK;
    }
    s_rx_queue = xQueueCreate(config->rx_queue_len, sizeof(rb_espnow_rx_t));
    s_event_queue = xQueueCreate(config->event_queue_len, sizeof(rb_event_t));
    s_snapshot_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_rx_queue && s_event_queue && s_snapshot_lock, ESP_ERR_NO_MEM, TAG, "no memory");
    return ESP_OK;
}

esp_err_t rb_controller_start(const rb_controller_config_t *config, const rb_controller_hooks_t *hooks)
{
    ESP_RETURN_ON_FALSE(config != NULL && safety_config_valid(&config->safety), ESP_ERR_INVALID_ARG, TAG,
                        "invalid safety configuration");
    ESP_RETURN_ON_FALSE(node_set_config_valid(&config->nodes), ESP_ERR_INVALID_ARG, TAG,
                        "invalid sensor node IDs: every node needs its own non-zero ID");
    ESP_RETURN_ON_ERROR(rb_controller_init(config), TAG, "queues");
    s_cfg = *config;
    if (hooks != NULL) {
        s_hooks = *hooks;
    }
    ESP_RETURN_ON_FALSE(xTaskCreate(safety_task, "safety", SAFETY_TASK_STACK, NULL, config->safety_task_priority, NULL) ==
                            pdPASS,
                        ESP_ERR_NO_MEM, TAG, "safety task");
    ESP_LOGI(TAG, "safety task running at priority %lu, tick %lu ms", (unsigned long)config->safety_task_priority,
             (unsigned long)config->tick_ms);
    if (config->nodes.presence_node_count == 1) {
        ESP_LOGW(TAG, "sensor nodes: presence node_%02lu (ONE radar only), thermal node_%02lu",
                 (unsigned long)config->nodes.presence_node_ids[0], (unsigned long)config->nodes.thermal_node_id);
    } else {
        ESP_LOGI(TAG, "sensor nodes: presence node_%02lu + node_%02lu, thermal node_%02lu",
                 (unsigned long)config->nodes.presence_node_ids[0], (unsigned long)config->nodes.presence_node_ids[1],
                 (unsigned long)config->nodes.thermal_node_id);
    }
    return ESP_OK;
}

QueueHandle_t rb_controller_rx_queue(void)
{
    return s_rx_queue;
}

bool rb_controller_inject(const rb_packet_t *packet)
{
    rb_espnow_rx_t item = {.packet = *packet, .rx_ms = rb_time_mono_ms()};
    return s_rx_queue != NULL && xQueueSend(s_rx_queue, &item, 0) == pdTRUE;
}

bool rb_controller_next_event(rb_event_t *out, TickType_t wait)
{
    return s_event_queue != NULL && xQueueReceive(s_event_queue, out, wait) == pdTRUE;
}

void rb_controller_get_snapshot(rb_snapshot_t *out)
{
    memset(out, 0, sizeof(*out));
    if (s_snapshot_lock != NULL && xSemaphoreTake(s_snapshot_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        *out = s_snapshot;
        xSemaphoreGive(s_snapshot_lock);
    }
}

uint32_t rb_controller_events_dropped(void)
{
    portENTER_CRITICAL(&s_req_lock);
    const uint32_t n = s_events_dropped;
    portEXIT_CRITICAL(&s_req_lock);
    return n;
}

uint32_t rb_controller_foreign_packets(void)
{
    return s_foreign_packets;
}

void rb_controller_request_reset(void)
{
    portENTER_CRITICAL(&s_req_lock);
    s_reset_requested = true;
    portEXIT_CRITICAL(&s_req_lock);
}

esp_err_t rb_controller_set_safety_config(const safety_config_t *config, bool test_timers)
{
    ESP_RETURN_ON_FALSE(safety_config_valid(config), ESP_ERR_INVALID_ARG, TAG, "invalid safety configuration");
    portENTER_CRITICAL(&s_req_lock);
    s_pending_config = *config;
    s_pending_test_timers = test_timers;
    s_config_pending = true;
    portEXIT_CRITICAL(&s_req_lock);
    return ESP_OK;
}
