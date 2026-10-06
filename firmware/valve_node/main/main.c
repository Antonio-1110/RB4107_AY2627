/*
 * Valve node: a hobby servo turning a gas ball valve (demo stand-in for a
 * motorised gas valve). It closes the valve when the controller says so, and
 * also whenever it stops hearing that it is safe.
 *
 *   wired:    controller output 2 ── input GPIO (pull-up) → servo
 *   wireless: controller ── ESP-NOW VALVE_COMMAND → servo, VALVE_STATUS back
 *
 * The open/close decision is in components/valve_logic. See README.md for
 * wiring and flashing.
 */
#include <inttypes.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "rb_config.h"
#include "rb_espnow.h"
#include "rb_log.h"
#include "rb_protocol.h"
#include "rb_time.h"
#include "servo.h"
#include "valve_logic.h"

static const char *TAG = "VALVE";

#define LOOP_MS 20
#define HEALTH_LOG_MS 10000

#if CONFIG_RB_VALVE_LATCH_CLOSED
#define LATCH_CLOSED true
#else
#define LATCH_CLOSED false
#endif

static void move(bool open)
{
    const uint32_t angle = open ? CONFIG_RB_VALVE_OPEN_ANGLE_DEG : CONFIG_RB_VALVE_CLOSED_ANGLE_DEG;
    const esp_err_t err = servo_set_angle(angle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "servo move to %" PRIu32 " deg failed: %s", angle, esp_err_to_name(err));
    }
}

static void log_change(const valve_logic_t *v)
{
    if (v->open) {
        ESP_LOGI(TAG, "valve OPEN (%d deg)", CONFIG_RB_VALVE_OPEN_ANGLE_DEG);
    } else {
        ESP_LOGW(TAG, "valve CLOSED (%d deg): %s%s", CONFIG_RB_VALVE_CLOSED_ANGLE_DEG, valve_reason_name(v->reason),
                 v->latched ? ", latched until reset" : "");
    }
}

/* ---- wired mode ---- */
#if CONFIG_RB_VALVE_MODE_WIRED

static esp_err_t link_start(void)
{
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << CONFIG_RB_VALVE_WIRED_INPUT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "input GPIO %d", CONFIG_RB_VALVE_WIRED_INPUT_GPIO);
    ESP_LOGI(TAG, "wired mode: input GPIO %d with pull-up; LOW = keep open, HIGH = close",
             CONFIG_RB_VALVE_WIRED_INPUT_GPIO);
    return ESP_OK;
}

static void link_poll(valve_logic_t *v, uint32_t now)
{
    const bool low = gpio_get_level(CONFIG_RB_VALVE_WIRED_INPUT_GPIO) == 0;
    if (low != v->line_low) {
        ESP_LOGI(TAG, "input %s", low ? "LOW (controller output ON: safe)" : "HIGH (controller output OFF)");
    }
    valve_logic_on_line(v, low, now);
}

static void link_after_tick(const valve_logic_t *v, uint32_t now, bool moved)
{
}

static void link_health(const valve_logic_t *v)
{
    ESP_LOGI(TAG, "%s | input %s", v->open ? "OPEN" : "CLOSED", v->line_low ? "LOW" : "HIGH");
}

static const valve_logic_config_t LOGIC_CFG = {
    .mode = VALVE_MODE_WIRED,
    .open_delay_ms = CONFIG_RB_VALVE_WIRED_OPEN_DELAY_MS,
    .close_debounce_ms = CONFIG_RB_VALVE_WIRED_CLOSE_DEBOUNCE_MS,
    .latch_closed = LATCH_CLOSED,
};

/* ---- wireless mode ---- */
#else

static uint8_t s_controller[6];
static bool s_any_controller;
static QueueHandle_t s_rx;
static uint32_t s_last_cmd_seq;
static uint32_t s_last_status_ms;
static uint32_t s_commands;

static esp_err_t link_start(void)
{
    ESP_RETURN_ON_ERROR(rb_espnow_parse_mac(CONFIG_RB_NODE_CONTROLLER_MAC, s_controller), TAG,
                        "bad RB_NODE_CONTROLLER_MAC '%s'", CONFIG_RB_NODE_CONTROLLER_MAC);
    static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    s_any_controller = memcmp(s_controller, BROADCAST, 6) == 0;

    s_rx = xQueueCreate(8, sizeof(rb_espnow_rx_t));
    ESP_RETURN_ON_FALSE(s_rx != NULL, ESP_ERR_NO_MEM, TAG, "rx queue");
    ESP_RETURN_ON_ERROR(rb_espnow_start(CONFIG_RB_ESPNOW_CHANNEL), TAG, "ESP-NOW start");
    ESP_RETURN_ON_ERROR(rb_espnow_add_peer(s_controller), TAG, "add peer");
    ESP_RETURN_ON_ERROR(rb_espnow_start_receiver(s_rx), TAG, "ESP-NOW receiver");

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "wireless mode: node %d, my MAC " MACSTR " (put it in the controller's RB_CTRL_VALVE_NODE_MAC)",
             CONFIG_RB_NODE_ID, MAC2STR(mac));
    ESP_LOGI(TAG, "controller " MACSTR "%s, closing after %d ms without a keep-open", MAC2STR(s_controller),
             s_any_controller ? " (broadcast: accepting commands from any controller)" : "",
             CONFIG_RB_VALVE_LINK_TIMEOUT_MS);
    return ESP_OK;
}

static void link_poll(valve_logic_t *v, uint32_t now)
{
    rb_espnow_rx_t rx;
    while (xQueueReceive(s_rx, &rx, 0) == pdTRUE) {
        const rb_packet_t *p = &rx.packet;
        if (p->type != RB_MSG_VALVE_COMMAND || p->body.valve_command.valve_node_id != CONFIG_RB_NODE_ID) {
            continue; /* another node's traffic */
        }
        if (!s_any_controller && memcmp(rx.src_mac, s_controller, 6) != 0) {
            RB_LOG_EVERY_MS(5000, ESP_LOGW, TAG, "ignored VALVE_COMMAND from " MACSTR " (not RB_NODE_CONTROLLER_MAC)",
                            MAC2STR(rx.src_mac));
            continue;
        }
        s_commands++;
        s_last_cmd_seq = p->sequence;
        valve_logic_on_command(v, p->body.valve_command.command, now);
    }
}

static void send_status(const valve_logic_t *v, uint32_t now)
{
    rb_packet_t pkt = {
        .type = RB_MSG_VALVE_STATUS,
        .role = RB_NODE_ROLE_VALVE,
        .node_id = CONFIG_RB_NODE_ID,
        .uptime_ms = now,
    };
    uint8_t flags = 0;
    flags |= !rb_time_elapsed(now, v->changed_ms, CONFIG_RB_VALVE_TRAVEL_MS) ? RB_VALVE_FLAG_MOVING : 0;
    flags |= v->latched ? RB_VALVE_FLAG_LATCHED : 0;
    pkt.body.valve_status = (rb_valve_status_t){
        .position = v->open ? RB_VALVE_POS_OPEN : RB_VALVE_POS_CLOSED,
        .flags = flags,
        .reason = v->reason,
        .last_command_seq = s_last_cmd_seq,
    };
    const esp_err_t err = rb_espnow_send(s_controller, &pkt);
    if (err != ESP_OK) {
        RB_LOG_EVERY_MS(5000, ESP_LOGW, TAG, "VALVE_STATUS not sent: %s", esp_err_to_name(err));
    }
    s_last_status_ms = now;
}

static void link_after_tick(const valve_logic_t *v, uint32_t now, bool moved)
{
    if (moved || rb_time_elapsed(now, s_last_status_ms, CONFIG_RB_VALVE_STATUS_PERIOD_MS)) {
        send_status(v, now);
    }
}

static void link_health(const valve_logic_t *v)
{
    rb_espnow_tx_stats_t st;
    rb_espnow_get_tx_stats(&st);
    ESP_LOGI(TAG, "%s | commands received %" PRIu32 " | status delivered %" PRIu32 " failed %" PRIu32,
             v->open ? "OPEN" : "CLOSED", s_commands, st.delivered, st.failed);
}

static const valve_logic_config_t LOGIC_CFG = {
    .mode = VALVE_MODE_WIRELESS,
    .link_timeout_ms = CONFIG_RB_VALVE_LINK_TIMEOUT_MS,
    .latch_closed = LATCH_CLOSED,
};

#endif

void app_main(void)
{
    rb_log_init();
    ESP_LOGI(TAG, "RB4107 valve node on %s", CONFIG_IDF_TARGET);

    const servo_config_t servo = {
        .gpio = CONFIG_RB_VALVE_SERVO_GPIO,
        .min_pulse_us = CONFIG_RB_VALVE_SERVO_MIN_PULSE_US,
        .max_pulse_us = CONFIG_RB_VALVE_SERVO_MAX_PULSE_US,
        .range_deg = CONFIG_RB_VALVE_SERVO_RANGE_DEG,
    };
    ESP_ERROR_CHECK(servo_init(&servo));

    static valve_logic_t valve;
    valve_logic_init(&valve, &LOGIC_CFG, rb_time_mono_ms());
    move(false); /* fail closed: start closed until the controller says it is safe */
    log_change(&valve);

    /* Without its input the node can never open the valve, which is the safe outcome. */
    ESP_ERROR_CHECK(link_start());

    uint32_t next_health = rb_time_mono_ms() + HEALTH_LOG_MS;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
        const uint32_t now = rb_time_mono_ms();
        const bool was_open = valve.open;
        link_poll(&valve, now); /* a wireless command can open or close the valve right here */
        const bool open = valve_logic_tick(&valve, now);
        const bool moved = open != was_open;
        if (moved) {
            move(open);
            log_change(&valve);
        }
        link_after_tick(&valve, now, moved);
        if ((int32_t)(now - next_health) >= 0) {
            next_health = now + HEALTH_LOG_MS;
            link_health(&valve);
        }
    }
}
