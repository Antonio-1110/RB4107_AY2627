/*
 * TODO section 20: MQTT topic structure.
 *
 * Prints the complete topic tree for the configured prefix and node, with
 * each topic's QoS/retain policy. Once connected, it publishes one sample
 * message to every topic, so the tree can be checked with
 * `tools/mqtt/watch.sh` on the MacBook.
 */
#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rb_config.h"
#include "rb_mqtt.h"
#include "rb_net.h"
#include "rb_topics.h"

static const char *TAG = "MQTT";

static char s_status_topic[64];
static bool s_started;

static void on_net(rb_net_state_t state, void *ctx)
{
    if (state == RB_NET_CONNECTED && !s_started) {
        rb_mqtt_config_t cfg = rb_mqtt_config_from_kconfig();
        cfg.status_topic = s_status_topic;
        cfg.online_payload = "{\"online\":true}";
        cfg.offline_payload = "{\"online\":false}";
        s_started = rb_mqtt_start(&cfg, NULL, NULL) == ESP_OK;
    }
}

void app_main(void)
{
    const char *prefix = CONFIG_RB_MQTT_TOPIC_PREFIX;
    rb_topic_build(RB_TOPIC_CONTROLLER_STATUS, prefix, 0, s_status_topic, sizeof(s_status_topic));

    ESP_LOGI(TAG, "topic tree for prefix '%s', node %d:", prefix, CONFIG_RB_CTRL_NODE_ID);
    for (int t = 0; t < RB_TOPIC_COUNT; t++) {
        char topic[96];
        rb_topic_build(t, prefix, CONFIG_RB_CTRL_NODE_ID, topic, sizeof(topic));
        const rb_topic_info_t *info = rb_topic_info(t);
        ESP_LOGI(TAG, "  %-40s QoS %u%s", topic, info->qos, info->retain ? ", retained" : "");
    }

    ESP_ERROR_CHECK(rb_net_start(on_net, NULL));
    while (rb_mqtt_state() != RB_MQTT_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    /* Skip controller/status: it only ever carries the online/offline flag. */
    for (int t = RB_TOPIC_CONTROLLER_STATUS + 1; t < RB_TOPIC_COUNT; t++) {
        char topic[96];
        rb_topic_build(t, prefix, CONFIG_RB_CTRL_NODE_ID, topic, sizeof(topic));
        const rb_topic_info_t *info = rb_topic_info(t);
        char payload[96];
        snprintf(payload, sizeof(payload), "{\"test\":\"topic structure\",\"topic_index\":%d}", t);
        rb_mqtt_publish(topic, payload, info->qos, false); /* not retained: test messages only */
    }
    ESP_LOGI(TAG, "published one test message per topic");
}
