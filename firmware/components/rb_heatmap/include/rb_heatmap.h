#pragma once

/*
 * Heat-map pictures on the controller (display only).
 *
 *   ESP-NOW callback -> thermal_frame_assemble() -> latest complete picture
 *                                                    -> telemetry task -> MQTT
 *
 * THERMAL_FRAME pieces never enter the safety task's queue, and nothing here
 * is read by the safety logic. Only the configured thermal node's pictures
 * are kept.
 */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "thermal_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t node_id;
    uint32_t frame_number;   /* node's picture counter (restarts when the node reboots) */
    uint32_t rx_ms;          /* controller monotonic time the last piece arrived */
    thermal_frame_t frame;
} rb_heatmap_frame_t;

typedef struct {
    thermal_frame_stats_t frames;
    uint32_t foreign_pieces;   /* from a node other than the thermal node */
    uint32_t not_taken;        /* complete pictures replaced before the telemetry task took them */
} rb_heatmap_stats_t;

/* Accept pictures from thermal_node_id. Call before rb_espnow_start_receiver(). */
esp_err_t rb_heatmap_start(uint32_t thermal_node_id);

/* The newest complete picture, if there is one the caller hasn't taken yet. */
bool rb_heatmap_take(rb_heatmap_frame_t *out);

void rb_heatmap_get_stats(rb_heatmap_stats_t *out);

#ifdef __cplusplus
}
#endif
