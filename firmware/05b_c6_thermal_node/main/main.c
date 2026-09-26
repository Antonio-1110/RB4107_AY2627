/*
 * TODO sections 1, 3.1 and 5: ESP32-C6 thermal node (one MLX90640).
 *
 *   MLX90640 → C6 → feature extraction → ESP-NOW (THERMAL_DATA) → S3
 *
 * The node reads frames, extracts the thermal features (never sends raw
 * frames) and transmits them. The controller (firmware 29) runs the safety
 * state machine.
 */
#include <stdio.h>
#include "esp_log.h"
#include "node_thermal.h"
#include "rb_log.h"
#include "rb_node_board.h"
#include "rb_node_link.h"

static uint16_t sample(rb_packet_t *data, void *ctx)
{
    bool no_data;
    data->type = RB_MSG_THERMAL_DATA;
    node_thermal_get(&data->body.thermal, &no_data);
    if (no_data) {
        return RB_FAULT_MLX_NO_DATA;
    }
    return data->body.thermal.valid ? 0 : RB_FAULT_MLX_INVALID;
}

static void describe(const rb_packet_t *data, char *buf, size_t len)
{
    const thermal_reading_t *t = &data->body.thermal;
    if (t->valid) {
        snprintf(buf, len, "thermal ok max=%.1fC hot-region=%.1fC", t->max_temp_c, t->hot_region_temp_c);
    } else {
        snprintf(buf, len, "thermal INVALID");
    }
}

void app_main(void)
{
    rb_log_init();
    rb_node_board_start("thermal node (MLX90640)");

    /* The thermal task keeps retrying a missing sensor; meanwhile the node reports it as a fault. */
    ESP_ERROR_CHECK(node_thermal_start());
    const rb_node_link_config_t link = {.role = RB_NODE_ROLE_THERMAL, .sample = sample, .describe = describe};
    ESP_ERROR_CHECK(rb_node_link_start(&link));
}
