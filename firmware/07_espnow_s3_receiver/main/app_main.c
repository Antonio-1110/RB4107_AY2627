/*
 * TODO section 7: ESP-NOW receiver on the ESP32-S3.
 *
 * The ESP-NOW callback validates each frame (size, magic, version, type,
 * CRC) and queues it without blocking. A receive task checks the node ID,
 * tracks sequence numbers (gaps, duplicates, out-of-order packets, node
 * restarts) and the last-packet time, and updates the sensor-node state.
 */
#include <inttypes.h>
#include <math.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "rb_config.h"
#include "rb_espnow.h"
#include "sensor_node.h"

static const char *TAG = "ESPNOW";

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void log_summary(const sensor_node_state_t *node)
{
    rb_espnow_rx_stats_t rx;
    rb_espnow_get_rx_stats(&rx);
    ESP_LOGI(TAG, "rx ok=%" PRIu32 " bad(len=%" PRIu32 " magic=%" PRIu32 " ver=%" PRIu32 " type=%" PRIu32 " crc=%" PRIu32 ") overflow=%" PRIu32,
             rx.received, rx.bad_length, rx.bad_magic, rx.bad_version, rx.bad_type, rx.bad_crc, rx.queue_overflow);
    ESP_LOGI(TAG, "node %" PRIu32 ": packets=%" PRIu32 " missed=%" PRIu32 " dup=%" PRIu32 " ooo=%" PRIu32 " restarts=%" PRIu32 " wrong-node=%" PRIu32 " last seq=%" PRIu32 " %" PRIu32 " ms ago, faults=0x%04x",
             node->node_id, node->packets, node->missed, node->duplicates, node->out_of_order, node->restarts,
             node->wrong_node, node->last_sequence, node->packets ? now_ms() - node->last_received_ms : 0,
             node->node_fault_flags);
    if (node->has_data) {
        const rb_sensor_data_t *d = &node->latest_data;
        ESP_LOGI("SENSOR", "presence %s%s%s%s dist=%.2fm | thermal %s max=%.1f hot=%.1f rate=%.2f px=%u",
                 d->presence.valid ? "valid" : "INVALID", d->presence.presence_detected ? " detected" : "",
                 d->presence.moving_target ? " moving" : "", d->presence.stationary_target ? " stationary" : "",
                 d->presence.distance_m, d->thermal.valid ? "valid" : "INVALID", d->thermal.max_temp_c,
                 d->thermal.hot_region_temp_c, d->thermal.temp_rate_c_per_min, d->thermal.pixels_above_threshold);
    }
}

static void receive_task(void *arg)
{
    QueueHandle_t queue = arg;
    sensor_node_state_t node;
    sensor_node_init(&node, CONFIG_RB_CTRL_NODE_ID);
    uint32_t next_log = 0;

    for (;;) {
        rb_espnow_rx_t item;
        if (xQueueReceive(queue, &item, pdMS_TO_TICKS(100)) == pdTRUE) {
            const node_seq_result_t res = sensor_node_on_packet(&node, &item.packet, item.rx_ms);
            switch (res) {
            case NODE_SEQ_OK:
                break;
            case NODE_SEQ_WRONG_NODE:
                ESP_LOGW(TAG, "dropped packet from unknown node %" PRIu32 " (" MACSTR ")", item.packet.node_id,
                         MAC2STR(item.src_mac));
                break;
            default:
                ESP_LOGW(TAG, "%s seq=%" PRIu32 " from node %" PRIu32 " rssi=%d", node_seq_result_name(res),
                         item.packet.sequence, item.packet.node_id, item.rssi);
                break;
            }
            if (item.packet.type == RB_MSG_SENSOR_FAULT && res != NODE_SEQ_DUPLICATE && res != NODE_SEQ_OUT_OF_ORDER) {
                ESP_LOGW("SENSOR", "node %" PRIu32 " faults now 0x%04x (changed 0x%04x)", item.packet.node_id,
                         item.packet.body.fault.fault_flags, item.packet.body.fault.changed_flags);
            }
        }
        if ((int32_t)(now_ms() - next_log) >= 0) {
            next_log = now_ms() + CONFIG_RB_S3_HEALTH_LOG_PERIOD_MS;
            log_summary(&node);
        }
    }
}

void app_main(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_ERROR_CHECK(rb_espnow_start(CONFIG_RB_ESPNOW_CHANNEL));
    ESP_LOGI(TAG, "receiver MAC " MACSTR ", channel %d, expecting node %d", MAC2STR(mac), CONFIG_RB_ESPNOW_CHANNEL,
             CONFIG_RB_CTRL_NODE_ID);

    QueueHandle_t queue = xQueueCreate(CONFIG_RB_CTRL_RX_QUEUE_LEN, sizeof(rb_espnow_rx_t));
    ESP_ERROR_CHECK(queue ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(rb_espnow_start_receiver(queue));
    xTaskCreate(receive_task, "espnow_rx", 4096, queue, 8, NULL);
}
