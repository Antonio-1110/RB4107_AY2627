#include "mqtt_client_app.h"

#include "esp_log.h"
#include "esp_check.h"
#include "mqtt_client.h"
#include "network.h"
#include "system_config.h"

static const char *TAG = "MQTT";
static esp_mqtt_client_handle_t client;
static bool connected;

static esp_err_t publish(const char *topic, const char *payload)
{
    if (!MQTT_ENABLE || !connected || client == NULL || payload == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_mqtt_client_publish(client, topic, payload, 0, 1, 0) < 0 ? ESP_FAIL : ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    (void)event_data;
    connected = event_id == MQTT_EVENT_CONNECTED;
    ESP_LOGI(TAG, "MQTT %s", connected ? "connected" : "disconnected");
}

esp_err_t mqtt_app_init(void)
{
    if (!MQTT_ENABLE)
    {
        ESP_LOGW(TAG, "MQTT is disabled until broker configuration is supplied");
        return ESP_OK;
    }
    esp_mqtt_client_config_t config = {.broker.address.uri = MQTT_BROKER_URI};
    client = esp_mqtt_client_init(&config);
    if (client == NULL)
    {
        return ESP_FAIL;
    }
    ESP_RETURN_ON_ERROR(esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL), TAG, "event registration failed");
    return esp_mqtt_client_start(client);
}

esp_err_t mqtt_publish_sensor_data(const char *payload)
{
    return publish(MQTT_TOPIC_SENSORS, payload);
}

esp_err_t mqtt_publish_event(const char *payload)
{
    return publish(MQTT_TOPIC_ALARM, payload);
}

bool mqtt_is_connected(void)
{
    return connected;
}