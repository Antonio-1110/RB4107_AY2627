#pragma once

/*
 * Remote C4002 tuning on the S3 (docs/c4002_tuning.md).
 *
 *   dashboard -> Django -> MQTT <prefix>/sensors/<node>/c4002_set
 *     -> this relay -> ESP-NOW C4002_CONFIG -> presence node
 *   presence node -> ESP-NOW C4002_CONFIG_ACK -> this relay
 *     -> telemetry task -> MQTT <prefix>/sensors/<node>/c4002_config (retained)
 *   presence node -> ESP-NOW C4002_LIVE -> this relay
 *     -> telemetry task -> MQTT <prefix>/sensors/<node>/c4002_live (QoS 0)
 *
 * The relay never touches the safety state machine. It learns each presence
 * node's MAC from the packets the node sends, so it can only reach a node
 * that has been heard from since boot. When it first hears a node, it asks
 * for the node's settings (request_id 0), so the dashboard shows them
 * without anyone pressing a button.
 */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "rb_c4002_cmd.h"
#include "rb_espnow.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Subscribes to the command topic. Call before the MQTT client starts (rb_connectivity_start). */
esp_err_t rb_c4002_relay_start(const uint32_t *presence_node_ids, uint32_t count);

/* Safety task: every accepted packet from a configured node (learns MACs, takes ACKs). Never blocks. */
void rb_c4002_relay_on_packet(const rb_espnow_rx_t *rx);

/*
 * Telemetry task: sends any automatic settings request, times out unanswered
 * commands, and returns one reply to publish (with node_id) if there is one.
 * Call it repeatedly until it returns false. reply->hdr is left for the caller.
 */
bool rb_c4002_relay_poll(uint32_t now_ms, rb_c4002_reply_t *reply, uint32_t *node_id);

/*
 * Telemetry task: the newest raw C4002 result of one node, if one came in
 * since the last call. Call it repeatedly until it returns false. msg->hdr is
 * left for the caller.
 */
bool rb_c4002_relay_poll_live(rb_c4002_live_msg_t *msg, uint32_t *node_id);

#ifdef __cplusplus
}
#endif
