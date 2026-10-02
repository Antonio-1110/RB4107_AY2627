#include "rb_heatmap.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "rb_espnow.h"

/* ~2.4 KB in total, all static. */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_node_id;
static thermal_frame_assembler_t s_assembler;   /* Wi-Fi task only */
static rb_heatmap_frame_t s_done;               /* Wi-Fi task only: just completed */
static rb_heatmap_frame_t s_latest;             /* shared, under s_lock */
static bool s_latest_ready;
static rb_heatmap_stats_t s_stats;              /* under s_lock */

/* Runs in the Wi-Fi task for every valid THERMAL_FRAME piece. */
static void on_piece(const rb_packet_t *hdr, const rb_thermal_frame_piece_t *piece, void *ctx)
{
    (void)ctx;
    if (hdr->node_id != s_node_id) {
        portENTER_CRITICAL(&s_lock);
        s_stats.foreign_pieces++;
        portEXIT_CRITICAL(&s_lock);
        return;
    }
    const thermal_frame_result_t res =
        thermal_frame_assemble(&s_assembler, hdr->node_id, hdr->sequence, piece, &s_done.frame);
    portENTER_CRITICAL(&s_lock);
    s_stats.frames = s_assembler.stats;
    if (res == THERMAL_FRAME_COMPLETE) {
        s_done.node_id = hdr->node_id;
        s_done.frame_number = hdr->sequence;
        s_done.rx_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (s_latest_ready) {
            s_stats.not_taken++;
        }
        s_latest = s_done;
        s_latest_ready = true;
    }
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t rb_heatmap_start(uint32_t thermal_node_id)
{
    s_node_id = thermal_node_id;
    thermal_frame_assembler_init(&s_assembler);
    rb_espnow_set_frame_handler(on_piece, NULL);
    return ESP_OK;
}

bool rb_heatmap_take(rb_heatmap_frame_t *out)
{
    portENTER_CRITICAL(&s_lock);
    const bool ready = s_latest_ready;
    if (ready) {
        *out = s_latest;
        s_latest_ready = false;
    }
    portEXIT_CRITICAL(&s_lock);
    return ready;
}

void rb_heatmap_get_stats(rb_heatmap_stats_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_stats;
    portEXIT_CRITICAL(&s_lock);
}
