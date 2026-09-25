#include "rb_connectivity.h"

#include "esp_log.h"
#include "fault_manager.h"
#include "rb_config.h"
#include "rb_json.h"
#include "rb_mqtt.h"
#include "rb_net.h"
#include "rb_topics.h"
#include "rb_wallclock.h"

static const char *TAG = "NET";

static char s_status_topic[96];
static char s_online[128];
static char s_offline[128];
static bool s_services_started;

static void on_mqtt_state(rb_mqtt_state_t state, void *ctx)
{
    fault_set(FAULT_MQTT_DOWN, state != RB_MQTT_CONNECTED, (int32_t)state);
}

static void on_net_state(rb_net_state_t state, void *ctx)
{
    fault_set(FAULT_NETWORK_DOWN, state != RB_NET_CONNECTED, (int32_t)state);
    if (state != RB_NET_CONNECTED || s_services_started) {
        return;
    }
    s_services_started = true;
    rb_wallclock_start_sntp();

    rb_mqtt_config_t cfg = rb_mqtt_config_from_kconfig();
    cfg.status_topic = s_status_topic;
    cfg.online_payload = s_online;
    cfg.offline_payload = s_offline;
    if (rb_mqtt_start(&cfg, on_mqtt_state, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "MQTT client not started (check RB_BROKER_HOST); telemetry disabled, safety unaffected");
    }
}

esp_err_t rb_connectivity_start(void)
{
    rb_topic_build(RB_TOPIC_CONTROLLER_STATUS, CONFIG_RB_MQTT_TOPIC_PREFIX, 0, s_status_topic, sizeof(s_status_topic));
    rb_json_controller_status(CONFIG_RB_MQTT_CONTROLLER_ID, true, s_online, sizeof(s_online));
    rb_json_controller_status(CONFIG_RB_MQTT_CONTROLLER_ID, false, s_offline, sizeof(s_offline));

#if CONFIG_RB_NET_NONE
    return ESP_OK;
#else
    fault_raise(FAULT_NETWORK_DOWN, 0);
    fault_raise(FAULT_MQTT_DOWN, 0);
    return rb_net_start(on_net_state, NULL);
#endif
}
