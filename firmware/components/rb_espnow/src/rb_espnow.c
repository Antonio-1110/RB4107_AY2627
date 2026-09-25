#include "rb_espnow.h"

#include <stdio.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "rb_wifi.h"

static const char *TAG = "ESPNOW";

_Static_assert(RB_ESPNOW_MAX_PAYLOAD == ESP_NOW_MAX_DATA_LEN, "RB_ESPNOW_MAX_PAYLOAD out of sync with ESP-IDF");

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static rb_espnow_tx_stats_t s_tx;
static uint32_t s_sequence;
static bool s_started;
static QueueHandle_t s_rx_queue;
static rb_espnow_rx_stats_t s_rx;

static void on_sent(const esp_now_send_info_t *info, esp_now_send_status_t status)
{
    (void)info;
    portENTER_CRITICAL(&s_lock);
    if (status == ESP_NOW_SEND_SUCCESS) {
        s_tx.delivered++;
        s_tx.consecutive_failures = 0;
        s_tx.last_delivered_ms = (uint32_t)(esp_timer_get_time() / 1000);
    } else {
        s_tx.failed++;
        s_tx.consecutive_failures++;
    }
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t rb_espnow_start(uint8_t channel)
{
    if (s_started) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(rb_wifi_base_start(), TAG, "wifi");
    if (channel != 0) {
        ESP_RETURN_ON_ERROR(rb_wifi_set_channel(channel), TAG, "channel %u", channel);
    }
    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "esp_now_init");
    ESP_RETURN_ON_ERROR(esp_now_register_send_cb(on_sent), TAG, "send cb");
    s_started = true;
    ESP_LOGI(TAG, "ESP-NOW started on channel %u", rb_wifi_get_channel());
    return ESP_OK;
}

esp_err_t rb_espnow_parse_mac(const char *text, uint8_t mac[6])
{
    unsigned v[6];
    char extra;
    if (text == NULL ||
        sscanf(text, "%x:%x:%x:%x:%x:%x%c", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &extra) != 6) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int i = 0; i < 6; i++) {
        if (v[i] > 0xFF) {
            return ESP_ERR_INVALID_ARG;
        }
        mac[i] = (uint8_t)v[i];
    }
    return ESP_OK;
}

esp_err_t rb_espnow_add_peer(const uint8_t mac[6])
{
    if (esp_now_is_peer_exist(mac)) {
        return ESP_OK;
    }
    esp_now_peer_info_t peer = {
        .channel = 0, /* follow the current channel */
        .ifidx = WIFI_IF_STA,
        .encrypt = false,
    };
    memcpy(peer.peer_addr, mac, 6);
    return esp_now_add_peer(&peer);
}

esp_err_t rb_espnow_send(const uint8_t mac[6], rb_packet_t *pkt)
{
    uint8_t buf[RB_PKT_MAX_LEN];
    pkt->protocol_version = RB_PROTOCOL_VERSION;
    portENTER_CRITICAL(&s_lock);
    pkt->sequence = ++s_sequence;
    portEXIT_CRITICAL(&s_lock);

    const size_t len = rb_protocol_encode(pkt, buf, sizeof(buf));
    ESP_RETURN_ON_FALSE(len > 0, ESP_ERR_INVALID_ARG, TAG, "encode failed");
    esp_err_t err = esp_now_send(mac, buf, len);
    portENTER_CRITICAL(&s_lock);
    if (err == ESP_OK) {
        s_tx.sent++;
    } else {
        s_tx.failed++;
        s_tx.consecutive_failures++;
    }
    portEXIT_CRITICAL(&s_lock);
    return err;
}

void rb_espnow_get_tx_stats(rb_espnow_tx_stats_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_tx;
    portEXIT_CRITICAL(&s_lock);
}

static void on_received(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    rb_espnow_rx_t item;
    const rb_decode_result_t res = rb_protocol_decode(data, len > 0 ? (size_t)len : 0, &item.packet);

    portENTER_CRITICAL(&s_lock);
    switch (res) {
    case RB_DECODE_OK: break;
    case RB_DECODE_ERR_LENGTH: s_rx.bad_length++; break;
    case RB_DECODE_ERR_MAGIC: s_rx.bad_magic++; break;
    case RB_DECODE_ERR_VERSION: s_rx.bad_version++; break;
    case RB_DECODE_ERR_TYPE: s_rx.bad_type++; break;
    case RB_DECODE_ERR_CRC: s_rx.bad_crc++; break;
    }
    portEXIT_CRITICAL(&s_lock);
    if (res != RB_DECODE_OK) {
        return;
    }

    memcpy(item.src_mac, info->src_addr, 6);
    item.rssi = info->rx_ctrl != NULL ? (int8_t)info->rx_ctrl->rssi : 0;
    item.rx_ms = (uint32_t)(esp_timer_get_time() / 1000);
    /* Runs in the Wi-Fi task: never block here. */
    const bool queued = xQueueSend(s_rx_queue, &item, 0) == pdTRUE;
    portENTER_CRITICAL(&s_lock);
    if (queued) {
        s_rx.received++;
    } else {
        s_rx.queue_overflow++;
    }
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t rb_espnow_start_receiver(QueueHandle_t queue)
{
    ESP_RETURN_ON_FALSE(s_started && queue != NULL, ESP_ERR_INVALID_STATE, TAG, "start ESP-NOW first");
    s_rx_queue = queue;
    return esp_now_register_recv_cb(on_received);
}

void rb_espnow_get_rx_stats(rb_espnow_rx_stats_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_rx;
    portEXIT_CRITICAL(&s_lock);
}
