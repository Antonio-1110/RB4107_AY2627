#include "rb_node_link.h"

#include <inttypes.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rb_config.h"
#include "rb_espnow.h"

static const char *TAG = "ESPNOW";

#define LINK_TASK_STACK 4096
#define LINK_TASK_PRIO 6
#define TICK_MS 50

static rb_node_link_config_t s_cfg;
static uint8_t s_peer[6];

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void send(rb_packet_t *pkt)
{
    pkt->role = s_cfg.role;
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

        rb_packet_t data = {0};
        const uint16_t faults = s_cfg.sample(&data, s_cfg.ctx);

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
            send(&data);
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
            char reading[96];
            s_cfg.describe(&data, reading, sizeof(reading));
            ESP_LOGI(TAG, "tx sent=%" PRIu32 " delivered=%" PRIu32 " failed=%" PRIu32 " (consecutive %" PRIu32
                          ") | %s | faults 0x%04x",
                     st.sent, st.delivered, st.failed, st.consecutive_failures, reading, faults);
            if (st.consecutive_failures >= 10) {
                ESP_LOGW(TAG, "controller not acknowledging: check RB_NODE_CONTROLLER_MAC and RB_ESPNOW_CHANNEL");
            }
        }
    }
}

esp_err_t rb_node_link_start(const rb_node_link_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL && config->sample != NULL && config->describe != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "sample/describe callbacks required");
    s_cfg = *config;
    ESP_RETURN_ON_ERROR(rb_espnow_parse_mac(CONFIG_RB_NODE_CONTROLLER_MAC, s_peer), TAG,
                        "bad RB_NODE_CONTROLLER_MAC '%s'", CONFIG_RB_NODE_CONTROLLER_MAC);
    ESP_RETURN_ON_ERROR(rb_espnow_start(CONFIG_RB_ESPNOW_CHANNEL), TAG, "ESP-NOW start");
    ESP_RETURN_ON_ERROR(rb_espnow_add_peer(s_peer), TAG, "add peer");
    ESP_LOGI(TAG, "sending to " MACSTR " as %s node %d", MAC2STR(s_peer), rb_node_role_name(s_cfg.role),
             CONFIG_RB_NODE_ID);
    return xTaskCreate(link_task, "espnow_link", LINK_TASK_STACK, NULL, LINK_TASK_PRIO, NULL) == pdPASS
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}
