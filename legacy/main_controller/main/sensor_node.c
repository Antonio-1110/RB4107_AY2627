#include "sensor_node.h"

#include <math.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "sensor_manager.h"
#include "system_config.h"

static const char *TAG = "SENSOR_NODE";

typedef struct __attribute__((packed))
{
    uint16_t magic;
    uint8_t version;
    uint8_t validity;
    uint32_t sequence;
    float temperature;
    float distance;
    uint8_t presence_detected;
} sensor_packet_t;

#define SENSOR_PACKET_MAGIC 0x5242U
#define SENSOR_PACKET_VERSION 1U
#define SENSOR_VALID_TEMPERATURE (1U << 0)
#define SENSOR_VALID_PRESENCE (1U << 1)
#define SENSOR_VALID_DISTANCE (1U << 2)

static void sensor_node_receive_callback(const esp_now_recv_info_t *info, const uint8_t *data, int data_len)
{
    (void)info;
    if (data == NULL || data_len != sizeof(sensor_packet_t))
    {
        return;
    }
    sensor_packet_t packet;
    memcpy(&packet, data, sizeof(packet));
    if (packet.magic != SENSOR_PACKET_MAGIC || packet.version != SENSOR_PACKET_VERSION)
    {
        return;
    }
    sensor_data_t update = {
        .temperature = packet.temperature,
        .presence_detected = packet.presence_detected != 0,
        .distance = packet.distance,
        .timestamp_us = esp_timer_get_time(),
        .temperature_valid = (packet.validity & SENSOR_VALID_TEMPERATURE) != 0 && isfinite(packet.temperature),
        .presence_valid = (packet.validity & SENSOR_VALID_PRESENCE) != 0,
        .distance_valid = (packet.validity & SENSOR_VALID_DISTANCE) != 0 && isfinite(packet.distance),
        .sensor_node_connected = true,
    };
    (void)sensor_manager_update(&update);
}

esp_err_t sensor_node_init(void)
{
    if (!SENSOR_NODE_ENABLE)
    {
        ESP_LOGW(TAG, "ESP-NOW sensor node is disabled");
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "ESP-NOW init failed");
    ESP_RETURN_ON_ERROR(esp_now_register_recv_cb(sensor_node_receive_callback), TAG, "ESP-NOW callback registration failed");
    ESP_LOGI(TAG, "ESP-NOW receiver ready; MAC allow-list is pending");
    return ESP_OK;
}