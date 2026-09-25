/*
 * TODO section 5: ESP32-C6 sensor node with the ESP-NOW sender.
 *
 *   C4002 ──┐
 *           ├─→ C6 → ESP-NOW → S3
 *   MLX90640┘
 *
 * The node only acquires, filters, extracts features and transmits. The
 * safety state machine runs on the S3. This is the sensor-node firmware used
 * in the end-to-end system (sections 29 and 30).
 */
#include "esp_log.h"
#include "esp_mac.h"
#include "node_link.h"
#include "node_thermal.h"
#include "rb_config.h"
#include "rb_log.h"
#include "rb_node_sensors.h"

static const char *TAG = "NODE";

void app_main(void)
{
    rb_log_init();
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "RB4107 sensor node %d, MAC " MACSTR, CONFIG_RB_NODE_ID, MAC2STR(mac));

    /* A failed sensor is reported as a fault over ESP-NOW; it doesn't stop the node. */
    if (rb_node_c4002_start() != ESP_OK) {
        ESP_LOGE(TAG, "C4002 not configured; presence will be reported INVALID until it responds");
    }
    ESP_ERROR_CHECK(node_thermal_start());
    ESP_ERROR_CHECK(node_link_start());
}
