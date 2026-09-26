#pragma once

/*
 * ESP-NOW transport for the RB4107 protocol.
 * The sender side is TODO section 5, the receiver side section 7.
 */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "rb_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t sent;              /* esp_now_send() accepted */
    uint32_t delivered;         /* MAC-layer ACK received */
    uint32_t failed;            /* no ACK, or esp_now_send() refused */
    uint32_t consecutive_failures;
    uint32_t last_delivered_ms;
} rb_espnow_tx_stats_t;

/* Start Wi-Fi (if needed) and ESP-NOW on the given channel (0 = keep current). */
esp_err_t rb_espnow_start(uint8_t channel);

/* Parse "AA:BB:CC:DD:EE:FF". */
esp_err_t rb_espnow_parse_mac(const char *text, uint8_t mac[6]);

esp_err_t rb_espnow_add_peer(const uint8_t mac[6]);

/*
 * Stamp pkt with the next sequence number, encode it and queue it for
 * transmission. Delivery is reported asynchronously through the stats.
 */
esp_err_t rb_espnow_send(const uint8_t mac[6], rb_packet_t *pkt);

void rb_espnow_get_tx_stats(rb_espnow_tx_stats_t *out);

/* ---- Receiver (controller side) ---- */

/* One validated packet, as posted to the receive queue. */
typedef struct {
    rb_packet_t packet;
    uint8_t src_mac[6];
    int8_t rssi;
    uint32_t rx_ms;             /* controller monotonic time of reception */
} rb_espnow_rx_t;

typedef struct {
    uint32_t received;          /* valid packets queued */
    uint32_t bad_length;
    uint32_t bad_magic;
    uint32_t bad_version;
    uint32_t bad_type;
    uint32_t bad_crc;
    uint32_t queue_overflow;    /* dropped because the consumer fell behind */
} rb_espnow_rx_stats_t;

/*
 * Validate every received frame (length, magic, version, type, CRC) and post
 * good ones to queue (item type rb_espnow_rx_t) without blocking. Call
 * rb_espnow_start() first.
 */
esp_err_t rb_espnow_start_receiver(QueueHandle_t queue);

void rb_espnow_get_rx_stats(rb_espnow_rx_stats_t *out);

#ifdef __cplusplus
}
#endif
