#pragma once

/*
 * ESP-NOW transport for the RB4107 protocol.
 * The sender side is TODO section 5, the receiver side section 7.
 */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
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

#ifdef __cplusplus
}
#endif
