#include "rb_mqtt.h"

#include <stdio.h>
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "mqtt_client.h"
#include "rb_config.h"

static const char *TAG = "MQTT";

static esp_mqtt_client_handle_t s_client;
static rb_mqtt_config_t s_cfg;
static volatile rb_mqtt_state_t s_state = RB_MQTT_DISCONNECTED;
static rb_mqtt_state_cb_t s_cb;
static void *s_cb_ctx;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static rb_mqtt_stats_t s_stats;

rb_mqtt_config_t rb_mqtt_config_from_kconfig(void)
{
    rb_mqtt_config_t cfg = {
        .client_id = CONFIG_RB_MQTT_CONTROLLER_ID,
    };
    snprintf(cfg.uri, sizeof(cfg.uri), "mqtt://%s:%d", CONFIG_RB_BROKER_HOST, CONFIG_RB_BROKER_PORT);
    return cfg;
}

static void set_state(rb_mqtt_state_t state)
{
    if (state == s_state) {
        return;
    }
    s_state = state;
    if (s_cb != NULL) {
        s_cb(state, s_cb_ctx);
    }
}

static void count(uint32_t *counter)
{
    portENTER_CRITICAL(&s_lock);
    (*counter)++;
    portEXIT_CRITICAL(&s_lock);
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_BEFORE_CONNECT:
        set_state(RB_MQTT_CONNECTING);
        break;
    case MQTT_EVENT_CONNECTED:
        count(&s_stats.connects);
        ESP_LOGI(TAG, "broker connected (%s)", s_cfg.uri);
        set_state(RB_MQTT_CONNECTED);
        if (s_cfg.status_topic != NULL && s_cfg.online_payload != NULL) {
            esp_mqtt_client_enqueue(s_client, s_cfg.status_topic, s_cfg.online_payload, 0, 1, 1, true);
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        count(&s_stats.disconnects);
        ESP_LOGW(TAG, "broker disconnected; retrying every %d ms", CONFIG_RB_MQTT_RECONNECT_MS);
        set_state(RB_MQTT_DISCONNECTED);
        break;
    case MQTT_EVENT_ERROR: {
        const esp_mqtt_event_handle_t event = data;
        if (event->error_handle != NULL && event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGD(TAG, "transport error (errno %d)", event->error_handle->esp_transport_sock_errno);
        }
        break;
    }
    default:
        break;
    }
}

esp_err_t rb_mqtt_start(const rb_mqtt_config_t *config, rb_mqtt_state_cb_t cb, void *ctx)
{
    ESP_RETURN_ON_FALSE(config != NULL && config->uri[0] != '\0', ESP_ERR_INVALID_ARG, TAG, "no broker URI");
    ESP_RETURN_ON_FALSE(CONFIG_RB_BROKER_HOST[0] != '\0', ESP_ERR_INVALID_ARG, TAG,
                        "RB_BROKER_HOST is empty: set the MacBook IP in menuconfig");
    s_cfg = *config;
    s_cb = cb;
    s_cb_ctx = ctx;

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = s_cfg.uri,
        .credentials.client_id = s_cfg.client_id,
        .session = {
            .keepalive = CONFIG_RB_MQTT_KEEPALIVE_S,
            .last_will = {
                .topic = s_cfg.status_topic,
                .msg = s_cfg.offline_payload,
                .qos = 1,
                .retain = 1,
            },
        },
        .network.reconnect_timeout_ms = CONFIG_RB_MQTT_RECONNECT_MS,
        .outbox.limit = (uint64_t)CONFIG_RB_MQTT_OUTBOX_LIMIT_KB * 1024,
    };
    s_client = esp_mqtt_client_init(&mqtt_cfg);
    ESP_RETURN_ON_FALSE(s_client != NULL, ESP_FAIL, TAG, "client init");
    ESP_RETURN_ON_ERROR(esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, on_event, NULL), TAG, "events");
    set_state(RB_MQTT_CONNECTING);
    ESP_LOGI(TAG, "connecting to %s as %s", s_cfg.uri, s_cfg.client_id);
    return esp_mqtt_client_start(s_client);
}

esp_err_t rb_mqtt_publish(const char *topic, const char *payload, int qos, bool retain)
{
    if (s_client == NULL || (qos == 0 && s_state != RB_MQTT_CONNECTED)) {
        count(&s_stats.dropped);
        return ESP_ERR_INVALID_STATE;
    }
    /* enqueue: the actual network I/O happens in the MQTT task, not here. */
    const int id = esp_mqtt_client_enqueue(s_client, topic, payload, 0, qos, retain, true);
    if (id < 0) {
        count(&s_stats.dropped);
        return id == -2 ? ESP_ERR_NO_MEM : ESP_FAIL;
    }
    count(&s_stats.published);
    return ESP_OK;
}

rb_mqtt_state_t rb_mqtt_state(void)
{
    return s_state;
}

const char *rb_mqtt_state_name(rb_mqtt_state_t state)
{
    static const char *const names[] = {"DISCONNECTED", "CONNECTING", "CONNECTED"};
    return (unsigned)state < sizeof(names) / sizeof(names[0]) ? names[state] : "?";
}

void rb_mqtt_get_stats(rb_mqtt_stats_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_stats;
    portEXIT_CRITICAL(&s_lock);
}
