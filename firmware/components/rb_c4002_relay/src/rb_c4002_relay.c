#include "rb_c4002_relay.h"

#include <stdio.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "rb_config.h"
#include "rb_mqtt.h"
#include "rb_time.h"
#include "rb_topics.h"

static const char *TAG = "TUNING";

#define MAX_NODES 2               /* presence nodes A and B */
#define REPLY_TIMEOUT_MS 8000     /* a full settings push is ~12 sensor commands */
#define AUTO_READ_RETRY_MS 30000  /* retry the automatic settings request until the node answers */
#define ERROR_LEN 64

typedef struct {
    uint32_t node_id;
    char name[16];                /* "node_01" */
    bool has_mac;
    uint8_t mac[6];
    bool want_read;               /* ask for the settings (request_id 0) */
    uint32_t next_read_ms;

    bool pending;                 /* a command is waiting for its ACK */
    uint16_t pending_request;
    uint8_t pending_action;
    uint32_t pending_deadline_ms;

    bool dirty;                   /* reply below not published yet */
    rb_c4002_reply_kind_t kind;
    uint16_t request_id;
    uint8_t action;
    char error[ERROR_LEN];
    bool has_ack;
    rb_c4002_ack_t ack;
} node_entry_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static node_entry_t s_nodes[MAX_NODES];
static uint32_t s_node_count;
static char s_out_error[ERROR_LEN];  /* poll() caller only */

static node_entry_t *find(uint32_t node_id)
{
    for (uint32_t i = 0; i < s_node_count; i++) {
        if (s_nodes[i].node_id == node_id) {
            return &s_nodes[i];
        }
    }
    return NULL;
}

/* strlcpy: no printf inside a critical section. */
static void copy_str(char *dst, const char *src, size_t cap)
{
    size_t i = 0;
    for (; src != NULL && src[i] != '\0' && i + 1 < cap; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

/* Call with s_lock held. */
static void set_reply(node_entry_t *n, rb_c4002_reply_kind_t kind, uint16_t request_id, uint8_t action,
                      const char *error)
{
    n->dirty = true;
    n->kind = kind;
    n->request_id = request_id;
    n->action = action;
    n->has_ack = false;
    copy_str(n->error, error, sizeof(n->error));
}

/* Encode and send one C4002_CONFIG. Not under s_lock (ESP-NOW calls). */
static esp_err_t send_config(const uint8_t mac[6], const rb_c4002_config_msg_t *msg)
{
    ESP_RETURN_ON_ERROR(rb_espnow_add_peer(mac), TAG, "add peer");
    rb_packet_t pkt = {.type = RB_MSG_C4002_CONFIG, .role = RB_NODE_ROLE_CONTROLLER, .node_id = 0};
    pkt.uptime_ms = rb_time_mono_ms();
    pkt.body.c4002_config = *msg;
    return rb_espnow_send(mac, &pkt);
}

/* MQTT task: one dashboard command. */
static void on_command(const char *topic, const char *data, size_t len, void *ctx)
{
    uint32_t node_id;
    if (!rb_c4002_cmd_topic_node(topic, CONFIG_RB_MQTT_TOPIC_PREFIX, &node_id)) {
        ESP_LOGW(TAG, "ignored command on %s", topic);
        return;
    }
    static rb_c4002_cmd_t cmd; /* MQTT task only; ~120 B off its stack */
    char err[ERROR_LEN];
    const bool ok = rb_c4002_cmd_parse(data, len, &cmd, err, sizeof(err));
    cmd.msg.target_node_id = node_id;
    if (ok && cmd.controller_id[0] != '\0' && strcmp(cmd.controller_id, CONFIG_RB_MQTT_CONTROLLER_ID) != 0) {
        ESP_LOGI(TAG, "command for controller %s ignored", cmd.controller_id);
        return;
    }

    portENTER_CRITICAL(&s_lock);
    node_entry_t *n = find(node_id);
    uint8_t mac[6];
    bool can_send = false;
    if (n == NULL) {
        /* Not one of our presence nodes: nobody to answer on behalf of. */
    } else if (!ok) {
        set_reply(n, RB_C4002_REPLY_REJECTED, cmd.msg.request_id, cmd.msg.action, err);
    } else if (!n->has_mac) {
        set_reply(n, RB_C4002_REPLY_UNKNOWN_NODE, cmd.msg.request_id, cmd.msg.action,
                  "nothing received from this node since the controller started");
    } else {
        memcpy(mac, n->mac, 6);
        can_send = true;
    }
    portEXIT_CRITICAL(&s_lock);

    if (n == NULL) {
        ESP_LOGW(TAG, "command for node_%02lu ignored: not a configured presence node", (unsigned long)node_id);
        return;
    }
    if (!ok) {
        ESP_LOGW(TAG, "node_%02lu command rejected: %s", (unsigned long)node_id, err);
        return;
    }
    if (!can_send) {
        ESP_LOGW(TAG, "node_%02lu command not sent: node not heard from yet", (unsigned long)node_id);
        return;
    }

    const esp_err_t send_err = send_config(mac, &cmd.msg);
    portENTER_CRITICAL(&s_lock);
    if (send_err != ESP_OK) {
        set_reply(n, RB_C4002_REPLY_SEND_FAILED, cmd.msg.request_id, cmd.msg.action, esp_err_to_name(send_err));
    } else {
        n->pending = true;
        n->pending_request = cmd.msg.request_id;
        n->pending_action = cmd.msg.action;
        n->pending_deadline_ms = rb_time_mono_ms() + REPLY_TIMEOUT_MS;
    }
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "node_%02lu request %u (%s) %s", (unsigned long)node_id, cmd.msg.request_id,
             rb_c4002_action_name(cmd.msg.action), send_err == ESP_OK ? "sent" : esp_err_to_name(send_err));
}

void rb_c4002_relay_on_packet(const rb_espnow_rx_t *rx)
{
    static const uint8_t no_mac[6] = {0};
    if (rx->packet.role != RB_NODE_ROLE_PRESENCE || memcmp(rx->src_mac, no_mac, 6) == 0) {
        return; /* simulated nodes (rb_sim) have no MAC and can't be tuned */
    }
    portENTER_CRITICAL(&s_lock);
    node_entry_t *n = find(rx->packet.node_id);
    if (n != NULL) {
        if (!n->has_mac || memcmp(n->mac, rx->src_mac, 6) != 0) {
            memcpy(n->mac, rx->src_mac, 6);
            n->has_mac = true;
            n->want_read = true;
            n->next_read_ms = rx->rx_ms;
        }
        if (rx->packet.type == RB_MSG_C4002_CONFIG_ACK) {
            const rb_c4002_ack_t *ack = &rx->packet.body.c4002_ack;
            if (n->pending && ack->request_id == n->pending_request) {
                n->pending = false;
            }
            set_reply(n, RB_C4002_REPLY_NODE, ack->request_id, ack->action, NULL);
            n->has_ack = true;
            n->ack = *ack;
            n->want_read = false;
        }
    }
    portEXIT_CRITICAL(&s_lock);
}

bool rb_c4002_relay_poll(uint32_t now_ms, rb_c4002_reply_t *reply, uint32_t *node_id)
{
    for (uint32_t i = 0; i < s_node_count; i++) {
        node_entry_t *n = &s_nodes[i];

        /* Automatic settings request after first hearing the node (or a new MAC). */
        portENTER_CRITICAL(&s_lock);
        const bool read_now = n->want_read && n->has_mac && !n->pending && (int32_t)(now_ms - n->next_read_ms) >= 0;
        uint8_t mac[6];
        memcpy(mac, n->mac, 6);
        if (read_now) {
            n->next_read_ms = now_ms + AUTO_READ_RETRY_MS;
        }
        portEXIT_CRITICAL(&s_lock);
        if (read_now) {
            const rb_c4002_config_msg_t msg = {.target_node_id = n->node_id, .action = RB_C4002_ACTION_READ};
            ESP_LOGI(TAG, "%s: asking for its C4002 settings", n->name);
            send_config(mac, &msg);
        }

        portENTER_CRITICAL(&s_lock);
        if (n->pending && (int32_t)(now_ms - n->pending_deadline_ms) >= 0) {
            n->pending = false;
            set_reply(n, RB_C4002_REPLY_NO_REPLY, n->pending_request, n->pending_action,
                      "the node did not answer (out of range, or older firmware?)");
        }
        bool out = n->dirty;
        if (out) {
            n->dirty = false;
            *node_id = n->node_id;
            copy_str(s_out_error, n->error, sizeof(s_out_error));
            *reply = (rb_c4002_reply_t){
                .sensor_node = n->name,
                .kind = n->kind,
                .request_id = n->request_id,
                .action = n->action,
                .error = s_out_error[0] != '\0' ? s_out_error : NULL,
                .has_ack = n->has_ack,
                .ack = n->ack,
            };
        }
        portEXIT_CRITICAL(&s_lock);
        if (out) {
            return true;
        }
    }
    return false;
}

esp_err_t rb_c4002_relay_start(const uint32_t *presence_node_ids, uint32_t count)
{
#if CONFIG_RB_CTRL_C4002_REMOTE_TUNING
    s_node_count = count > MAX_NODES ? MAX_NODES : count;
    for (uint32_t i = 0; i < s_node_count; i++) {
        s_nodes[i] = (node_entry_t){.node_id = presence_node_ids[i]};
        rb_topic_node_name(presence_node_ids[i], s_nodes[i].name, sizeof(s_nodes[i].name));
    }
    char filter[96];
    snprintf(filter, sizeof(filter), "%s/sensors/+/%s", CONFIG_RB_MQTT_TOPIC_PREFIX, RB_TOPIC_C4002_SET);
    ESP_RETURN_ON_ERROR(rb_mqtt_subscribe(filter, 1, on_command, NULL), TAG, "subscribe");
    ESP_LOGI(TAG, "remote C4002 tuning: listening on %s", filter);
#else
    (void)presence_node_ids;
    (void)count;
    ESP_LOGI(TAG, "remote C4002 tuning disabled in menuconfig");
#endif
    return ESP_OK;
}
