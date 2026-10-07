#include "rb_espnow.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "rb_log.h"
#include "rb_wifi.h"

static const char *TAG = "ESPNOW";

_Static_assert(RB_ESPNOW_MAX_PAYLOAD == ESP_NOW_MAX_DATA_LEN, "RB_ESPNOW_MAX_PAYLOAD out of sync with ESP-IDF");

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static rb_espnow_tx_stats_t s_tx;
static uint32_t s_reported; /* send callbacks so far: results come in the order the packets were sent */
static uint32_t s_sequence;
static bool s_started;
static QueueHandle_t s_rx_queue;
static rb_espnow_rx_stats_t s_rx;
static rb_espnow_frame_handler_t s_frame_handler;
static void *s_frame_ctx;
static TaskHandle_t s_follow_task;

static void on_sent(const esp_now_send_info_t *info, esp_now_send_status_t status)
{
    (void)info;
    portENTER_CRITICAL(&s_lock);
    s_reported++;
    if (status == ESP_NOW_SEND_SUCCESS) {
        s_tx.delivered++;
        s_tx.consecutive_failures = 0;
        s_tx.last_delivered_ms = (uint32_t)(esp_timer_get_time() / 1000);
    } else {
        s_tx.failed++;
        s_tx.consecutive_failures++;
    }
    portEXIT_CRITICAL(&s_lock);
    if (s_follow_task != NULL) {
        xTaskNotifyGive(s_follow_task); /* a probe may be waiting for this result */
    }
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

static esp_err_t send_encoded(const uint8_t mac[6], const uint8_t *buf, size_t len)
{
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

esp_err_t rb_espnow_send(const uint8_t mac[6], rb_packet_t *pkt)
{
    uint8_t buf[RB_PKT_MAX_LEN];
    pkt->protocol_version = RB_PROTOCOL_VERSION;
    portENTER_CRITICAL(&s_lock);
    pkt->sequence = ++s_sequence;
    portEXIT_CRITICAL(&s_lock);
    return send_encoded(mac, buf, rb_protocol_encode(pkt, buf, sizeof(buf)));
}

esp_err_t rb_espnow_send_frame_piece(const uint8_t mac[6], const rb_packet_t *hdr,
                                     const rb_thermal_frame_piece_t *piece)
{
    uint8_t buf[RB_PKT_THERMAL_FRAME_LEN];
    return send_encoded(mac, buf, rb_protocol_encode_frame_piece(hdr, piece, buf, sizeof(buf)));
}

void rb_espnow_get_tx_stats(rb_espnow_tx_stats_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_tx;
    portEXIT_CRITICAL(&s_lock);
}

/* ---- Following the controller's channel ---- */

#define FOLLOW_TASK_STACK 3072
#define FOLLOW_TASK_PRIO 5
#define FOLLOW_CHECK_MS 100
#define PROBE_SETTLE_MS 10  /* packets sent on the old channel report before the probe goes out */
#define PROBE_WAIT_MS 300   /* safety net: the probe's own result normally ends the wait much sooner */
#define SWEEP_PAUSE_MS 1000 /* between sweeps while the controller can't be found */
#define MAX_CHANNEL 13
#define NVS_NAMESPACE "rb_espnow"
#define NVS_KEY_CHANNEL "channel"

static volatile bool s_check_requested;

#if CONFIG_RB_ESPNOW_FOLLOW_CHANNEL

static rb_espnow_probe_fn_t s_probe;
static void *s_probe_ctx;

static uint8_t load_channel(void)
{
    nvs_handle_t nvs;
    uint8_t channel = 0;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u8(nvs, NVS_KEY_CHANNEL, &channel);
        nvs_close(nvs);
    }
    return channel >= 1 && channel <= MAX_CHANNEL ? channel : 0;
}

/* Only written when the channel changes, so flash wear is not a concern. */
static void save_channel(uint8_t channel)
{
    if (load_channel() == channel) {
        return;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, NVS_KEY_CHANNEL, channel);
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "channel %u not saved: %s", channel, esp_err_to_name(err));
    }
}

/* Move to channel, send one probe and report whether the controller acknowledged it. */
static bool probe_channel(uint8_t channel)
{
    if (rb_wifi_get_channel() != channel && rb_wifi_set_channel(channel) != ESP_OK) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(PROBE_SETTLE_MS));
    portENTER_CRITICAL(&s_lock);
    const uint32_t delivered = s_tx.delivered;
    portEXIT_CRITICAL(&s_lock);
    ulTaskNotifyTake(pdTRUE, 0);
    s_probe(s_probe_ctx);
    portENTER_CRITICAL(&s_lock);
    const uint32_t through_probe = s_tx.sent; /* the probe's result is in once this many have reported */
    portEXIT_CRITICAL(&s_lock);

    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(PROBE_WAIT_MS);
    for (;;) {
        portENTER_CRITICAL(&s_lock);
        const bool acked = s_tx.delivered != delivered; /* any acknowledged packet on this channel will do */
        const bool done = (int32_t)(s_reported - through_probe) >= 0;
        portEXIT_CRITICAL(&s_lock);
        const TickType_t left = deadline - xTaskGetTickCount();
        if (acked || done || (int32_t)left <= 0) {
            return acked;
        }
        ulTaskNotifyTake(pdTRUE, left);
    }
}

/* The current channel first, then the others in turn. Returns 0 if none acknowledged. */
static uint8_t search(uint8_t current)
{
    if (current >= 1 && current <= MAX_CHANNEL && probe_channel(current)) {
        return current;
    }
    for (int i = 1; i <= MAX_CHANNEL; i++) {
        const uint8_t channel = (uint8_t)((current + i - 1) % MAX_CHANNEL + 1);
        if (channel != current && probe_channel(channel)) {
            return channel;
        }
    }
    return 0;
}

static void follow_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(FOLLOW_CHECK_MS));
        rb_espnow_tx_stats_t st;
        rb_espnow_get_tx_stats(&st);
        const bool lost = st.consecutive_failures >= CONFIG_RB_ESPNOW_LOST_FAILURES;
        if (!lost && !s_check_requested) {
            continue;
        }
        s_check_requested = false;

        const uint8_t old = rb_wifi_get_channel();
        if (lost) {
            ESP_LOGW(TAG, "controller not acknowledging on channel %u: searching", old);
        }
        uint32_t sweeps = 1;
        uint8_t channel;
        while ((channel = search(old)) == 0) {
            RB_LOG_EVERY_MS(10000, ESP_LOGW, TAG, "no channel acknowledged after %" PRIu32 " sweeps: is the controller on?",
                            sweeps);
            sweeps++;
            vTaskDelay(pdMS_TO_TICKS(SWEEP_PAUSE_MS));
        }
        if (channel != old) {
            ESP_LOGW(TAG, "controller found on channel %u (was %u)", channel, old);
        } else if (lost || sweeps > 1) {
            ESP_LOGI(TAG, "controller acknowledging again on channel %u", channel);
        }
        save_channel(channel);
        s_check_requested = false; /* answered by this search */
    }
}

#endif

esp_err_t rb_espnow_follow_start(const uint8_t controller_mac[6], rb_espnow_probe_fn_t probe, void *ctx)
{
#if CONFIG_RB_ESPNOW_FOLLOW_CHANNEL
    ESP_RETURN_ON_FALSE(s_started && probe != NULL, ESP_ERR_INVALID_STATE, TAG, "start ESP-NOW first");
    static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (memcmp(controller_mac, BROADCAST, 6) == 0) {
        ESP_LOGW(TAG, "controller MAC is broadcast: staying on channel %u (set the controller's MAC to follow its "
                      "channel)", rb_wifi_get_channel());
        return ESP_OK;
    }
    if (s_follow_task != NULL) {
        return ESP_OK;
    }
    s_probe = probe;
    s_probe_ctx = ctx;
    const uint8_t saved = load_channel();
    if (saved != 0 && saved != rb_wifi_get_channel()) {
        ESP_RETURN_ON_ERROR(rb_wifi_set_channel(saved), TAG, "channel %u", saved);
        ESP_LOGI(TAG, "starting on channel %u, where the controller was last found", saved);
    }
    return xTaskCreate(follow_task, "espnow_follow", FOLLOW_TASK_STACK, NULL, FOLLOW_TASK_PRIO, &s_follow_task) ==
                   pdPASS
               ? ESP_OK
               : ESP_ERR_NO_MEM;
#else
    return ESP_OK;
#endif
}

void rb_espnow_follow_check(void)
{
    s_check_requested = true;
}

static void count_decode_error(rb_decode_result_t res)
{
    portENTER_CRITICAL(&s_lock);
    switch (res) {
    case RB_DECODE_OK: break;
    case RB_DECODE_ERR_LENGTH: s_rx.bad_length++; break;
    case RB_DECODE_ERR_MAGIC: s_rx.bad_magic++; break;
    case RB_DECODE_ERR_VERSION: s_rx.bad_version++; break;
    case RB_DECODE_ERR_TYPE: s_rx.bad_type++; break;
    case RB_DECODE_ERR_CRC: s_rx.bad_crc++; break;
    case RB_DECODE_ERR_ROLE: s_rx.bad_role++; break;
    }
    portEXIT_CRITICAL(&s_lock);
}

/* Heat-map pieces bypass the safety task's queue: they go straight to the frame handler. */
static void on_frame_piece(const uint8_t *data, size_t len)
{
    rb_packet_t hdr;
    rb_thermal_frame_piece_t piece;
    const rb_decode_result_t res = rb_protocol_decode_frame_piece(data, len, &hdr, &piece);
    if (res != RB_DECODE_OK) {
        count_decode_error(res);
        return;
    }
    if (s_frame_handler != NULL) {
        s_frame_handler(&hdr, &piece, s_frame_ctx);
        portENTER_CRITICAL(&s_lock);
        s_rx.frame_pieces++;
        portEXIT_CRITICAL(&s_lock);
    }
}

static void on_received(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (rb_protocol_is_frame_piece(data, len > 0 ? (size_t)len : 0)) {
        on_frame_piece(data, (size_t)len);
        return;
    }
    rb_espnow_rx_t item;
    const rb_decode_result_t res = rb_protocol_decode(data, len > 0 ? (size_t)len : 0, &item.packet);
    if (res != RB_DECODE_OK) {
        count_decode_error(res);
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

void rb_espnow_set_frame_handler(rb_espnow_frame_handler_t handler, void *ctx)
{
    s_frame_ctx = ctx;
    s_frame_handler = handler;
}

void rb_espnow_get_rx_stats(rb_espnow_rx_stats_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_rx;
    portEXIT_CRITICAL(&s_lock);
}
