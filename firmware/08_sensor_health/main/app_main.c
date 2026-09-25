/*
 * TODO section 8: sensor-node health monitoring on the ESP32-S3.
 *
 *   ONLINE -> STALE -> OFFLINE
 *
 * Builds on project 07. Every 100 ms the node is re-evaluated: link state
 * from the time since the last packet, and presence/thermal validity from
 * the latest data. Changes are logged as fault and recovery events.
 * Missing data comes out as presence UNKNOWN, never as "nobody there".
 *
 * To test: power the C6 off and on, unplug a sensor, or move out of range.
 */
#include <inttypes.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "rb_config.h"
#include "rb_espnow.h"
#include "sensor_node.h"

static const char *TAG = "SENSOR";

#define EVALUATE_PERIOD_MS 100

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static const char *tristate(rb_tristate_t v)
{
    return v == RB_TRUE ? "PRESENT" : v == RB_FALSE ? "ABSENT" : "UNKNOWN";
}

static void log_events(const sensor_node_state_t *node, uint32_t events)
{
    const uint32_t id = node->node_id;
    if (events & NODE_EVT_ONLINE) {
        ESP_LOGI(TAG, "node_%02" PRIu32 " ONLINE", id);
    }
    if (events & NODE_EVT_STALE) {
        ESP_LOGW(TAG, "node_%02" PRIu32 " STALE: no packet for %d ms", id, CONFIG_RB_CTRL_NODE_STALE_MS);
    }
    if (events & NODE_EVT_OFFLINE) {
        ESP_LOGE(TAG, "node_%02" PRIu32 " OFFLINE: no packet for %d ms", id, CONFIG_RB_CTRL_NODE_OFFLINE_MS);
    }
    if (events & NODE_EVT_PRESENCE_INVALID) {
        ESP_LOGW(TAG, "node_%02" PRIu32 " presence unavailable (node faults 0x%04x)", id, node->node_fault_flags);
    }
    if (events & NODE_EVT_PRESENCE_VALID) {
        ESP_LOGI(TAG, "node_%02" PRIu32 " presence restored", id);
    }
    if (events & NODE_EVT_THERMAL_INVALID) {
        ESP_LOGW(TAG, "node_%02" PRIu32 " thermal unavailable (node faults 0x%04x)", id, node->node_fault_flags);
    }
    if (events & NODE_EVT_THERMAL_VALID) {
        ESP_LOGI(TAG, "node_%02" PRIu32 " thermal restored", id);
    }
}

static void health_task(void *arg)
{
    QueueHandle_t queue = arg;
    const sensor_node_health_config_t cfg = {
        .stale_timeout_ms = CONFIG_RB_CTRL_NODE_STALE_MS,
        .offline_timeout_ms = CONFIG_RB_CTRL_NODE_OFFLINE_MS,
    };
    sensor_node_state_t node;
    sensor_node_init(&node, CONFIG_RB_CTRL_NODE_ID);
    uint32_t next_eval = 0, next_log = 0;

    for (;;) {
        rb_espnow_rx_t item;
        /* Wake for packets, but never sleep past the next evaluation. */
        if (xQueueReceive(queue, &item, pdMS_TO_TICKS(EVALUATE_PERIOD_MS)) == pdTRUE) {
            sensor_node_on_packet(&node, &item.packet, item.rx_ms);
        }
        const uint32_t now = now_ms();
        if ((int32_t)(now - next_eval) >= 0) {
            next_eval = now + EVALUATE_PERIOD_MS;
            log_events(&node, sensor_node_evaluate(&node, &cfg, now));
        }
        if ((int32_t)(now - next_log) >= 0) {
            next_log = now + CONFIG_RB_S3_HEALTH_LOG_PERIOD_MS;
            node_inputs_t in;
            sensor_node_inputs(&node, &in);
            ESP_LOGI(TAG, "node_%02" PRIu32 " %s | presence %s | thermal %s hot=%.1fC | missed=%" PRIu32 " restarts=%" PRIu32,
                     node.node_id, node_link_state_name(in.link), tristate(in.presence),
                     in.thermal_valid ? "valid" : "UNKNOWN", in.hot_region_temp_c, node.missed, node.restarts);
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(rb_espnow_start(CONFIG_RB_ESPNOW_CHANNEL));
    QueueHandle_t queue = xQueueCreate(CONFIG_RB_CTRL_RX_QUEUE_LEN, sizeof(rb_espnow_rx_t));
    ESP_ERROR_CHECK(queue ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(rb_espnow_start_receiver(queue));
    xTaskCreate(health_task, "node_health", 4096, queue, 8, NULL);
}
