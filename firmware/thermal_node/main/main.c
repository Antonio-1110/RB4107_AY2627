/*
 * TODO sections 1, 3.1 and 5: ESP32-C6 thermal node (one MLX90640).
 *
 *   MLX90640 → C6 → feature extraction → ESP-NOW (THERMAL_DATA) → S3
 *
 * The node reads frames, extracts the thermal features and transmits them.
 * The controller (firmware/controller) runs the safety state machine on those
 * features. Separately, and for display only, it sends a 1-byte-per-pixel
 * heat-map picture every RB_THERMAL_HEATMAP_PERIOD_MS (THERMAL_FRAME).
 */
#include <stdio.h>
#include "esp_log.h"
#include "node_thermal.h"
#include "rb_config.h"
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

#if CONFIG_RB_THERMAL_HEATMAP
/*
 * Heat-map pictures for the dashboard (display only). Each picture goes out
 * as 4 THERMAL_FRAME pieces, one per link tick, so they never crowd the
 * THERMAL_DATA, HEARTBEAT and SENSOR_FAULT packets the safety logic uses.
 */
static void send_heatmap(uint32_t now_ms, void *ctx)
{
    static thermal_frame_t frame;   /* the picture being sent (~780 bytes, not on the stack) */
    static uint32_t number;         /* its number; also the frame number on the wire */
    static uint8_t next_piece = RB_FRAME_PIECES;

    if (next_piece >= RB_FRAME_PIECES) {
        if (!node_thermal_get_heatmap(&frame, &number)) {
            return;
        }
        next_piece = 0;
    }
    rb_thermal_frame_piece_t piece;
    thermal_frame_piece(&frame, next_piece++, &piece);
    rb_node_link_send_frame_piece(number, &piece); /* a lost piece only drops this picture */
}
#endif

void app_main(void)
{
    rb_log_init();
    rb_node_board_start("thermal node (MLX90640)");

    /* The thermal task keeps retrying a missing sensor; meanwhile the node reports it as a fault. */
    ESP_ERROR_CHECK(node_thermal_start());
    const rb_node_link_config_t link = {
        .role = RB_NODE_ROLE_THERMAL,
        .sample = sample,
        .describe = describe,
#if CONFIG_RB_THERMAL_HEATMAP
        .extra = send_heatmap,
#endif
    };
    ESP_ERROR_CHECK(rb_node_link_start(&link));
}
