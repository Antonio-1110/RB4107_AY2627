/*
 * TODO sections 1 and 5: ESP32-C6 presence node (one C4002 radar).
 *
 *   C4002 → C6 → ESP-NOW (PRESENCE_DATA) → S3
 *
 * The system has two of these, flashed with node IDs 1 and 2 (see README).
 * The node only reads the radar and transmits. The controller (firmware 29)
 * combines both radars and runs the safety state machine.
 */
#include <stdio.h>
#include "c4002.h"
#include "esp_log.h"
#include "rb_config.h"
#include "rb_log.h"
#include "rb_node_board.h"
#include "rb_node_link.h"
#include "rb_node_sensors.h"

static const char *TAG = "NODE";

static uint16_t sample(rb_packet_t *data, void *ctx)
{
    data->type = RB_MSG_PRESENCE_DATA;
    c4002_get_reading(&data->body.presence);

    c4002_result_t raw;
    uint32_t age_ms = 0;
    const bool fresh = c4002_get_raw(&raw, &age_ms) && age_ms <= CONFIG_RB_C4002_STALE_TIMEOUT_MS;
    if (!fresh) {
        return RB_FAULT_C4002_NO_DATA;
    }
    return data->body.presence.valid ? 0 : RB_FAULT_C4002_INVALID;
}

static void describe(const rb_packet_t *data, char *buf, size_t len)
{
    const presence_reading_t *p = &data->body.presence;
    snprintf(buf, len, "presence %s", !p->valid ? "INVALID" : (p->presence_detected ? "present" : "absent"));
}

void app_main(void)
{
    rb_log_init();
    rb_node_board_start("presence node (C4002)");

    /* A failed sensor is reported as a fault over ESP-NOW; it doesn't stop the node. */
    if (rb_node_c4002_start() != ESP_OK) {
        ESP_LOGE(TAG, "C4002 not configured; presence will be reported INVALID until it responds");
    }
    const rb_node_link_config_t link = {.role = RB_NODE_ROLE_PRESENCE, .sample = sample, .describe = describe};
    ESP_ERROR_CHECK(rb_node_link_start(&link));
}
