#include "rb_mqtt.h"

#include <stdio.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "mqtt_client.h"
#include "rb_config.h"

static const char *TAG = "MQTT";

static esp_mqtt_client_handle_t s_client;
static rb_mqtt_config_t s_cfg;
static const char *volatile s_uri; /* s_cfg.uri or s_cfg.fallback_uri */
static volatile rb_mqtt_state_t s_state = RB_MQTT_DISCONNECTED;
static rb_mqtt_state_cb_t s_cb;
static void *s_cb_ctx;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static rb_mqtt_stats_t s_stats;

#define MAX_SUBSCRIPTIONS 2
#define TOPIC_MAX 128

typedef struct {
    char filter[TOPIC_MAX];
    int qos;
    rb_mqtt_rx_cb_t cb;
    void *ctx;
} subscription_t;

static subscription_t s_subs[MAX_SUBSCRIPTIONS];
static int s_sub_count;

rb_mqtt_config_t rb_mqtt_config_from_kconfig(void)
{
    rb_mqtt_config_t cfg = {
        .client_id = CONFIG_RB_MQTT_CONTROLLER_ID,
    };
    snprintf(cfg.uri, sizeof(cfg.uri), "mqtt://%s:%d", CONFIG_RB_BROKER_HOST, CONFIG_RB_BROKER_PORT);
    if (CONFIG_RB_BROKER_FALLBACK_HOST[0] != '\0') {
        snprintf(cfg.fallback_uri, sizeof(cfg.fallback_uri), "mqtt://%s:%d", CONFIG_RB_BROKER_FALLBACK_HOST,
                 CONFIG_RB_BROKER_PORT);
    }
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

/* Does an MQTT filter with '+' and a trailing '#' match topic? */
static bool topic_matches(const char *filter, const char *topic)
{
    while (*filter != '\0') {
        if (*filter == '#') {
            return true;
        }
        if (*filter == '+') {
            while (*topic != '\0' && *topic != '/') {
                topic++;
            }
            filter++;
            continue;
        }
        if (*filter != *topic) {
            return false;
        }
        filter++;
        topic++;
    }
    return *topic == '\0';
}

static void on_data(const esp_mqtt_event_handle_t event)
{
    /* Commands are small: anything split over several events is not one of ours. */
    if (event->current_data_offset != 0 || event->data_len != event->total_data_len || event->topic_len <= 0 ||
        event->topic_len >= TOPIC_MAX) {
        count(&s_stats.rx_dropped);
        ESP_LOGW(TAG, "dropped incoming message (%d bytes, topic %d chars)", event->total_data_len, event->topic_len);
        return;
    }
    char topic[TOPIC_MAX];
    memcpy(topic, event->topic, event->topic_len);
    topic[event->topic_len] = '\0';
    for (int i = 0; i < s_sub_count; i++) {
        if (topic_matches(s_subs[i].filter, topic)) {
            count(&s_stats.received);
            s_subs[i].cb(topic, event->data, (size_t)event->data_len, s_subs[i].ctx);
            return;
        }
    }
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_BEFORE_CONNECT:
        set_state(RB_MQTT_CONNECTING);
        break;
    case MQTT_EVENT_CONNECTED:
        count(&s_stats.connects);
        ESP_LOGI(TAG, "broker connected (%s)", s_uri);
        set_state(RB_MQTT_CONNECTED);
        if (s_cfg.status_topic != NULL && s_cfg.online_payload != NULL) {
            esp_mqtt_client_enqueue(s_client, s_cfg.status_topic, s_cfg.online_payload, 0, 1, 1, true);
        }
        for (int i = 0; i < s_sub_count; i++) {
            if (esp_mqtt_client_subscribe(s_client, s_subs[i].filter, s_subs[i].qos) < 0) {
                ESP_LOGW(TAG, "subscribe to %s failed", s_subs[i].filter);
            } else {
                ESP_LOGI(TAG, "subscribed to %s", s_subs[i].filter);
            }
        }
        break;
    case MQTT_EVENT_DATA:
        on_data(data);
        break;
    case MQTT_EVENT_DISCONNECTED:
        /* Also sent for every failed connection attempt: try the other host next. */
        count(&s_stats.disconnects);
        if (s_cfg.fallback_uri[0] != '\0') {
            s_uri = s_uri == s_cfg.uri ? s_cfg.fallback_uri : s_cfg.uri;
            esp_mqtt_client_set_uri(s_client, s_uri);
        }
        ESP_LOGW(TAG, "broker disconnected; next attempt in %d ms (%s)", CONFIG_RB_MQTT_RECONNECT_MS, s_uri);
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
    s_uri = s_cfg.uri;
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
    if (s_cfg.fallback_uri[0] != '\0') {
        ESP_LOGI(TAG, "connecting to %s (fallback %s) as %s", s_cfg.uri, s_cfg.fallback_uri, s_cfg.client_id);
    } else {
        ESP_LOGI(TAG, "connecting to %s as %s", s_cfg.uri, s_cfg.client_id);
    }
    return esp_mqtt_client_start(s_client);
}

esp_err_t rb_mqtt_publish(const char *topic, const char *payload, int qos, bool retain)
{
    if (s_client == NULL || (qos == 0 && s_state != RB_MQTT_CONNECTED)) {
        count(&s_stats.dropped);
        return ESP_ERR_INVALID_STATE;
    }
    /*
     * Send from the calling (telemetry) task. QoS 1 stays in the outbox until
     * acknowledged, and is queued automatically while disconnected. Using
     * esp_mqtt_client_enqueue() for everything was tried and rejected: QoS 0
     * items then pile up in the outbox faster than the MQTT task drains them
     * and crowd out the QoS 1 events.
     */
    const int id = esp_mqtt_client_publish(s_client, topic, payload, 0, qos, retain);
    if (id < 0) {
        count(&s_stats.dropped);
        return id == -2 ? ESP_ERR_NO_MEM : ESP_FAIL;
    }
    count(&s_stats.published);
    return ESP_OK;
}

esp_err_t rb_mqtt_subscribe(const char *filter, int qos, rb_mqtt_rx_cb_t cb, void *ctx)
{
    ESP_RETURN_ON_FALSE(s_client == NULL, ESP_ERR_INVALID_STATE, TAG, "subscribe before rb_mqtt_start()");
    ESP_RETURN_ON_FALSE(filter != NULL && cb != NULL && strlen(filter) < TOPIC_MAX, ESP_ERR_INVALID_ARG, TAG,
                        "bad subscription");
    ESP_RETURN_ON_FALSE(s_sub_count < MAX_SUBSCRIPTIONS, ESP_ERR_NO_MEM, TAG, "too many subscriptions");
    subscription_t *sub = &s_subs[s_sub_count++];
    snprintf(sub->filter, sizeof(sub->filter), "%s", filter);
    sub->qos = qos;
    sub->cb = cb;
    sub->ctx = ctx;
    return ESP_OK;
}

rb_mqtt_state_t rb_mqtt_state(void)
{
    return s_state;
}

const char *rb_mqtt_broker_uri(void)
{
    return s_uri != NULL ? s_uri : "";
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
