#include "node_link.h"

#include <inttypes.h>
#include "c4002.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "node_thermal.h"
#include "rb_config.h"
#include "rb_espnow.h"
#include "rb_protocol.h"

static const char *TAG = "ESPNOW";

#define LINK_TASK_STACK 4096
#define LINK_TASK_PRIO 6
#define TICK_MS 50

static uint8_t s_peer[6];

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static uint16_t sensor_fault_flags(const presence_reading_t *p, const thermal_reading_t *t, bool thermal_no_data)
{
    uint16_t flags = 0;
    c4002_result_t raw;
    uint32_t age_ms = 0;
    const bool c4002_fresh = c4002_get_raw(&raw, &age_ms) && age_ms <= CONFIG_RB_C4002_STALE_TIMEOUT_MS;
    if (!c4002_fresh) {
        flags |= RB_FAULT_C4002_NO_DATA;
    } else if (!p->valid) {
        flags |= RB_FAULT_C4002_INVALID;
    }
    if (thermal_no_data) {
        flags |= RB_FAULT_MLX_NO_DATA;
    } else if (!t->valid) {
        flags |= RB_FAULT_MLX_INVALID;
    }
    return flags;
}

static void send(rb_packet_t *pkt)
{
    pkt->node_id = CONFIG_RB_NODE_ID;
    pkt->uptime_ms = now_ms();
    esp_err_t err = rb_espnow_send(s_peer, pkt);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "%s not queued: %s", rb_msg_type_name(pkt->type), esp_err_to_name(err));
    }
}

static void link_task(void *arg)
{
    uint16_t last_faults = 0xFFFF; /* forces a first SENSOR_FAULT report */
    uint32_t next_data = 0, next_heartbeat = 0, next_log = 0;
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(TICK_MS));
        const uint32_t now = now_ms();

        presence_reading_t presence;
        thermal_reading_t thermal;
        bool thermal_no_data;
        c4002_get_reading(&presence);
        node_thermal_get(&thermal, &thermal_no_data);
        const uint16_t faults = sensor_fault_flags(&presence, &thermal, thermal_no_data);

        if (faults != last_faults) {
            rb_packet_t pkt = {.type = RB_MSG_SENSOR_FAULT};
            pkt.body.fault.fault_flags = faults;
            pkt.body.fault.changed_flags = last_faults == 0xFFFF ? faults : (uint16_t)(faults ^ last_faults);
            send(&pkt);
            ESP_LOGW(TAG, "sensor faults 0x%04x -> 0x%04x", last_faults == 0xFFFF ? 0 : last_faults, faults);
            last_faults = faults;
        }
        if ((int32_t)(now - next_data) >= 0) {
            next_data = now + CONFIG_RB_NODE_DATA_PERIOD_MS;
            rb_packet_t pkt = {.type = RB_MSG_SENSOR_DATA};
            pkt.body.sensor.presence = presence;
            pkt.body.sensor.thermal = thermal;
            send(&pkt);
        }
        if ((int32_t)(now - next_heartbeat) >= 0) {
            next_heartbeat = now + CONFIG_RB_NODE_HEARTBEAT_PERIOD_MS;
            rb_espnow_tx_stats_t st;
            rb_espnow_get_tx_stats(&st);
            rb_packet_t pkt = {.type = RB_MSG_HEARTBEAT};
            pkt.body.heartbeat = (rb_heartbeat_t){.fault_flags = faults, .tx_ok = st.delivered, .tx_fail = st.failed};
            send(&pkt);
        }
        if ((int32_t)(now - next_log) >= 0) {
            next_log = now + CONFIG_RB_NODE_HEALTH_LOG_PERIOD_MS;
            rb_espnow_tx_stats_t st;
            rb_espnow_get_tx_stats(&st);
            ESP_LOGI(TAG, "tx sent=%" PRIu32 " delivered=%" PRIu32 " failed=%" PRIu32 " (consecutive %" PRIu32 ") | presence %s%s | thermal %s max=%.1fC | faults 0x%04x",
                     st.sent, st.delivered, st.failed, st.consecutive_failures,
                     presence.valid ? "ok" : "INVALID",
                     presence.valid ? (presence.presence_detected ? " (present)" : " (absent)") : "",
                     thermal.valid ? "ok" : "INVALID", thermal.max_temp_c, faults);
            if (st.consecutive_failures >= 10) {
                ESP_LOGW(TAG, "controller not acknowledging: check RB_NODE_CONTROLLER_MAC and RB_ESPNOW_CHANNEL");
            }
        }
    }
}

esp_err_t node_link_start(void)
{
    ESP_RETURN_ON_ERROR(rb_espnow_parse_mac(CONFIG_RB_NODE_CONTROLLER_MAC, s_peer), TAG,
                        "bad RB_NODE_CONTROLLER_MAC '%s'", CONFIG_RB_NODE_CONTROLLER_MAC);
    ESP_RETURN_ON_ERROR(rb_espnow_start(CONFIG_RB_ESPNOW_CHANNEL), TAG, "ESP-NOW start");
    ESP_RETURN_ON_ERROR(rb_espnow_add_peer(s_peer), TAG, "add peer");
    ESP_LOGI(TAG, "sending to " MACSTR " as node %d", MAC2STR(s_peer), CONFIG_RB_NODE_ID);
    return xTaskCreate(link_task, "espnow_link", LINK_TASK_STACK, NULL, LINK_TASK_PRIO, NULL) == pdPASS
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}
