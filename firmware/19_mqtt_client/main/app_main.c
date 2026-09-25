/*
 * TODO section 19: MQTT client on the S3.
 *
 * Network up -> connect to Mosquitto on the MacBook -> publish a small
 * heartbeat every 5 s. The connection state (DISCONNECTED / CONNECTING /
 * CONNECTED) is logged on every change. Stop Mosquitto to see the disconnect
 * detected and automatic reconnection once it is back.
 *
 * Watch it on the MacBook with:  tools/mqtt/watch.sh
 */
#include <inttypes.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rb_config.h"
#include "rb_json.h"
#include "rb_mqtt.h"
#include "rb_net.h"

static const char *TAG = "MQTT";

static char s_status_topic[64];
static char s_online[128];
static char s_offline[128];
static char s_heartbeat_topic[64];
static bool s_mqtt_started;

static void on_mqtt_state(rb_mqtt_state_t state, void *ctx)
{
    ESP_LOGI(TAG, "MQTT state: %s", rb_mqtt_state_name(state));
}

static void on_net_state(rb_net_state_t state, void *ctx)
{
    ESP_LOGI("NET", "network %s", rb_net_state_name(state));
    if (state == RB_NET_CONNECTED && !s_mqtt_started) {
        rb_mqtt_config_t cfg = rb_mqtt_config_from_kconfig();
        cfg.status_topic = s_status_topic;
        rb_json_controller_status(CONFIG_RB_MQTT_CONTROLLER_ID, true, s_online, sizeof(s_online));
        rb_json_controller_status(CONFIG_RB_MQTT_CONTROLLER_ID, false, s_offline, sizeof(s_offline));
        cfg.online_payload = s_online;
        cfg.offline_payload = s_offline;
        s_mqtt_started = rb_mqtt_start(&cfg, on_mqtt_state, NULL) == ESP_OK;
    }
}

void app_main(void)
{
    snprintf(s_status_topic, sizeof(s_status_topic), "%s/controller/status", CONFIG_RB_MQTT_TOPIC_PREFIX);
    snprintf(s_heartbeat_topic, sizeof(s_heartbeat_topic), "%s/controller/heartbeat", CONFIG_RB_MQTT_TOPIC_PREFIX);
    ESP_ERROR_CHECK(rb_net_start(on_net_state, NULL));

    uint32_t beat = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        char payload[96];
        snprintf(payload, sizeof(payload), "{\"controller_id\":\"%s\",\"uptime_ms\":%" PRIu64 ",\"beat\":%" PRIu32 "}",
                 CONFIG_RB_MQTT_CONTROLLER_ID, (uint64_t)(esp_timer_get_time() / 1000), ++beat);
        rb_mqtt_publish(s_heartbeat_topic, payload, 0, false);

        rb_mqtt_stats_t st;
        rb_mqtt_get_stats(&st);
        ESP_LOGI(TAG, "%s | published %" PRIu32 " dropped %" PRIu32 " connects %" PRIu32 " disconnects %" PRIu32,
                 rb_mqtt_state_name(rb_mqtt_state()), st.published, st.dropped, st.connects, st.disconnects);
    }
}
