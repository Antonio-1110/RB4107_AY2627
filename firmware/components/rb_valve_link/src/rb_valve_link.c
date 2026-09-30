#include "rb_valve_link.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rb_config.h"
#include "rb_espnow.h"
#include "rb_log.h"
#include "rb_time.h"

static const char *TAG = "VALVE";

#define LINK_TASK_STACK 3072
#define LINK_TASK_PRIO 4
#define POLL_MS 50
/* The safety task reports every tick; this much silence means it has stopped. */
#define FEED_TIMEOUT_MS (3u * CONFIG_RB_CTRL_SAFETY_TICK_MS + 500u)

static uint8_t s_peer[6];
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_fed;
static bool s_shutdown;
static uint32_t s_fed_ms;
static bool s_have_status;
static rb_valve_status_t s_status;
static uint32_t s_status_ms;

void rb_valve_link_set_shutdown(bool shutdown)
{
    const uint32_t now = rb_time_mono_ms();
    portENTER_CRITICAL(&s_lock);
    s_fed = true;
    s_shutdown = shutdown;
    s_fed_ms = now;
    portEXIT_CRITICAL(&s_lock);
}

static const char *position_name(rb_valve_pos_t position)
{
    switch (position) {
    case RB_VALVE_POS_OPEN: return "OPEN";
    case RB_VALVE_POS_CLOSED: return "CLOSED";
    default: return "?";
    }
}

bool rb_valve_link_on_packet(const rb_packet_t *pkt, uint32_t rx_ms)
{
    if (pkt->type != RB_MSG_VALVE_STATUS || pkt->node_id != CONFIG_RB_CTRL_VALVE_NODE_ID) {
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    const bool changed = !s_have_status || s_status.position != pkt->body.valve_status.position;
    s_status = pkt->body.valve_status;
    s_status_ms = rx_ms;
    s_have_status = true;
    portEXIT_CRITICAL(&s_lock);
    if (changed) {
        ESP_LOGI(TAG, "valve node_%02lu reports %s (reason %u)", (unsigned long)pkt->node_id,
                 position_name(pkt->body.valve_status.position), (unsigned)pkt->body.valve_status.reason);
    }
    return true;
}

bool rb_valve_link_get_status(rb_valve_status_t *out, uint32_t *age_ms)
{
    const uint32_t now = rb_time_mono_ms();
    portENTER_CRITICAL(&s_lock);
    const bool have = s_have_status;
    *out = s_status;
    *age_ms = now - s_status_ms;
    portEXIT_CRITICAL(&s_lock);
    return have;
}

static void send_command(bool close)
{
    rb_packet_t pkt = {
        .type = RB_MSG_VALVE_COMMAND,
        .role = RB_NODE_ROLE_CONTROLLER,
        .node_id = 0, /* the controller */
        .uptime_ms = rb_time_mono_ms(),
    };
    pkt.body.valve_command = (rb_valve_command_t){
        .command = close ? RB_VALVE_CMD_CLOSE : RB_VALVE_CMD_KEEP_OPEN,
        .valve_node_id = CONFIG_RB_CTRL_VALVE_NODE_ID,
    };
    const esp_err_t err = rb_espnow_send(s_peer, &pkt);
    if (err != ESP_OK) {
        RB_LOG_EVERY_MS(5000, ESP_LOGW, TAG, "VALVE_COMMAND not sent: %s", esp_err_to_name(err));
    }
}

static void link_task(void *arg)
{
    bool sent_close = false;
    bool first = true;
    uint32_t last_send = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        const uint32_t now = rb_time_mono_ms();
        portENTER_CRITICAL(&s_lock);
        const bool fed = s_fed && !rb_time_elapsed(now, s_fed_ms, FEED_TIMEOUT_MS);
        const bool shutdown = s_shutdown;
        const bool have_status = s_have_status;
        const uint32_t status_age = now - s_status_ms;
        portEXIT_CRITICAL(&s_lock);

        /* No word from the safety task: nobody is vouching that it is safe. */
        const bool close = shutdown || !fed;
        if (first || close != sent_close || rb_time_elapsed(now, last_send, CONFIG_RB_CTRL_VALVE_KEEPALIVE_MS)) {
            if (first || close != sent_close) {
                if (close) {
                    ESP_LOGW(TAG, "commanding valve CLOSE (%s)", shutdown ? "shutdown" : "safety task not reporting");
                } else {
                    ESP_LOGI(TAG, "commanding valve KEEP_OPEN");
                }
            }
            send_command(close);
            sent_close = close;
            first = false;
            last_send = now;
        }
        if (!have_status || status_age > CONFIG_RB_CTRL_VALVE_STATUS_TIMEOUT_MS) {
            RB_LOG_EVERY_MS(10000, ESP_LOGW, TAG,
                            "no VALVE_STATUS from node_%02d: check RB_CTRL_VALVE_NODE_MAC, the node ID and the channel",
                            CONFIG_RB_CTRL_VALVE_NODE_ID);
        }
    }
}

esp_err_t rb_valve_link_start(void)
{
    ESP_RETURN_ON_ERROR(rb_espnow_parse_mac(CONFIG_RB_CTRL_VALVE_NODE_MAC, s_peer), TAG,
                        "bad RB_CTRL_VALVE_NODE_MAC '%s'", CONFIG_RB_CTRL_VALVE_NODE_MAC);
    ESP_RETURN_ON_ERROR(rb_espnow_add_peer(s_peer), TAG, "add peer");
    ESP_LOGI(TAG, "wireless valve node_%02d at " MACSTR ", keep-open every %d ms", CONFIG_RB_CTRL_VALVE_NODE_ID,
             MAC2STR(s_peer), CONFIG_RB_CTRL_VALVE_KEEPALIVE_MS);
    return xTaskCreate(link_task, "valve_link", LINK_TASK_STACK, NULL, LINK_TASK_PRIO, NULL) == pdPASS
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}
