#include "rb_c4002_remote.h"

#include <string.h>
#include "c4002.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "rb_config.h"
#include "rb_espnow.h"
#include "rb_node_link.h"
#include "rb_node_sensors.h"
#include "rb_protocol.h"

static const char *TAG = "TUNING";

#define REMOTE_TASK_STACK 4096
#define REMOTE_TASK_PRIO 4         /* below the link task: tuning never delays presence data */
#define RX_QUEUE_LEN 4
#define CALIB_GRACE_MS 3000        /* after the countdown, before reading the thresholds back */
#define CALIB_MAX_S 600
#define DUPLICATE_WINDOW_MS 10000  /* a repeated request_id within this window only gets its ACK again */

static QueueHandle_t s_queue;
static uint8_t s_controller[6];

/* Calibration in progress (task-local state). */
static bool s_calibrating;
static uint16_t s_calib_request;
static uint32_t s_calib_done_ms;

/* Last answered request, to absorb ESP-NOW retransmissions. */
static uint16_t s_last_request;
static uint32_t s_last_request_ms;
static rb_c4002_ack_t s_last_ack;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void send_ack(const rb_c4002_ack_t *ack)
{
    rb_packet_t pkt = {.type = RB_MSG_C4002_CONFIG_ACK};
    pkt.body.c4002_ack = *ack;
    if (rb_node_link_send(&pkt) != ESP_OK) {
        ESP_LOGW(TAG, "ACK for request %u not sent", ack->request_id);
    }
}

static rb_c4002_ack_t make_ack(uint16_t request_id, uint8_t action, uint8_t result)
{
    return (rb_c4002_ack_t){
        .request_id = request_id,
        .action = action,
        .result = result,
        .calib_remaining_s = s_calibrating ? c4002_calibration_remaining_s() : 0,
        .saved = rb_node_c4002_settings_saved(),
        .params = rb_node_c4002_settings(),
    };
}

/* Push new settings; on success they become current and are saved. */
static uint8_t apply_and_save(const c4002_settings_t *next)
{
    if (c4002_apply_settings(next) != ESP_OK) {
        /* The sensor may now be half-configured: try to restore the previous settings. */
        const c4002_settings_t prev = rb_node_c4002_settings();
        if (c4002_apply_settings(&prev) != ESP_OK) {
            ESP_LOGE(TAG, "C4002 also refused the previous settings");
        }
        return RB_C4002_RESULT_SENSOR_ERROR;
    }
    const esp_err_t err = rb_node_c4002_save(next);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "settings applied but not saved (%s): they last until the next reboot", esp_err_to_name(err));
    }
    rb_node_c4002_set_current(next, err == ESP_OK);
    return RB_C4002_RESULT_OK;
}

static uint8_t do_apply(const rb_c4002_config_msg_t *cmd)
{
    c4002_settings_t next = rb_node_c4002_settings();
    rb_c4002_params_merge(&next, &cmd->params, cmd->field_mask);
    const char *why = rb_c4002_params_check(&next);
    if (why != NULL) {
        ESP_LOGW(TAG, "request %u rejected: %s", cmd->request_id, why);
        return RB_C4002_RESULT_INVALID;
    }
    return apply_and_save(&next);
}

static uint8_t do_calibrate(const rb_c4002_config_msg_t *cmd)
{
    if (cmd->calib_duration_s == 0 || cmd->calib_duration_s > CALIB_MAX_S || cmd->calib_delay_s > CALIB_MAX_S) {
        ESP_LOGW(TAG, "request %u rejected: calibration delay 0..600 s, duration 1..600 s", cmd->request_id);
        return RB_C4002_RESULT_INVALID;
    }
    if (c4002_start_env_calibration(cmd->calib_delay_s, cmd->calib_duration_s) != ESP_OK) {
        return RB_C4002_RESULT_SENSOR_ERROR;
    }
    ESP_LOGW(TAG, "environment calibration starts in %u s and runs %u s: keep the area empty", cmd->calib_delay_s,
             cmd->calib_duration_s);
    s_calibrating = true;
    s_calib_request = cmd->request_id;
    s_calib_done_ms = now_ms() + (uint32_t)(cmd->calib_delay_s + cmd->calib_duration_s) * 1000u + CALIB_GRACE_MS;
    return RB_C4002_RESULT_OK;
}

/* Copy the thresholds the sensor uses now into settings (the arrays become "known"). */
static esp_err_t read_thresholds(c4002_settings_t *s)
{
    const unsigned n = rb_c4002_gate_count(s->resolution);
    ESP_RETURN_ON_ERROR(c4002_read_gate_thresholds(C4002_GATE_MOTION, n, s->motion_thresholds), TAG, "motion");
    ESP_RETURN_ON_ERROR(c4002_read_gate_thresholds(C4002_GATE_PRESENCE, n, s->presence_thresholds), TAG, "presence");
    s->thresholds_known = RB_C4002_THRESH_MOTION | RB_C4002_THRESH_PRESENCE;
    return ESP_OK;
}

static uint8_t do_read(void)
{
    c4002_settings_t s = rb_node_c4002_settings();
    if (read_thresholds(&s) != ESP_OK) {
        return RB_C4002_RESULT_SENSOR_ERROR;
    }
    /* Only the threshold arrays changed, and they are only used with CUSTOM sensitivity. */
    rb_node_c4002_set_current(&s, rb_node_c4002_settings_saved());
    return RB_C4002_RESULT_OK;
}

static uint8_t do_reset(void)
{
    if (rb_node_c4002_forget() != ESP_OK) {
        ESP_LOGW(TAG, "could not erase the saved settings");
    }
    const c4002_settings_t defaults = rb_node_c4002_defaults();
    if (c4002_apply_settings(&defaults) != ESP_OK) {
        return RB_C4002_RESULT_SENSOR_ERROR;
    }
    rb_node_c4002_set_current(&defaults, false);
    ESP_LOGI(TAG, "back to the menuconfig settings");
    return RB_C4002_RESULT_OK;
}

static void handle_command(const rb_espnow_rx_t *rx)
{
    const rb_c4002_config_msg_t *cmd = &rx->packet.body.c4002_config;
    if (rx->packet.type != RB_MSG_C4002_CONFIG || memcmp(rx->src_mac, s_controller, 6) != 0) {
        ESP_LOGW(TAG, "ignored %s from " MACSTR " (not the controller)", rb_msg_type_name(rx->packet.type),
                 MAC2STR(rx->src_mac));
        return;
    }
    if (cmd->target_node_id != (uint32_t)CONFIG_RB_NODE_ID) {
        ESP_LOGW(TAG, "ignored request %u for node %lu", cmd->request_id, (unsigned long)cmd->target_node_id);
        return;
    }
    const uint32_t now = now_ms();
    if (cmd->request_id != 0 && cmd->request_id == s_last_request && now - s_last_request_ms < DUPLICATE_WINDOW_MS) {
        send_ack(&s_last_ack);
        return;
    }

    ESP_LOGI(TAG, "request %u: %s (fields 0x%03x)", cmd->request_id, rb_c4002_action_name(cmd->action),
             cmd->field_mask);
    uint8_t result;
    switch (cmd->action) {
    case RB_C4002_ACTION_APPLY: result = do_apply(cmd); break;
    case RB_C4002_ACTION_CALIBRATE: result = do_calibrate(cmd); break;
    case RB_C4002_ACTION_READ: result = do_read(); break;
    case RB_C4002_ACTION_RESET: result = do_reset(); break;
    default: result = RB_C4002_RESULT_INVALID; break;
    }
    if (result != RB_C4002_RESULT_OK) {
        ESP_LOGW(TAG, "request %u: %s", cmd->request_id, rb_c4002_result_name(result));
    }
    s_last_ack = make_ack(cmd->request_id, cmd->action, result);
    s_last_request = cmd->request_id;
    s_last_request_ms = now;
    send_ack(&s_last_ack);
}

/* After a calibration: keep what the sensor learned, so it survives a reboot. */
static void finish_calibration(void)
{
    s_calibrating = false;
    c4002_settings_t s = rb_node_c4002_settings();
    uint8_t result = RB_C4002_RESULT_SENSOR_ERROR;
    if (read_thresholds(&s) == ESP_OK) {
        s.motion_sensitivity = RB_C4002_SENS_CUSTOM;
        s.presence_sensitivity = RB_C4002_SENS_CUSTOM;
        const esp_err_t err = rb_node_c4002_save(&s);
        rb_node_c4002_set_current(&s, err == ESP_OK);
        result = RB_C4002_RESULT_OK;
        ESP_LOGI(TAG, "calibration done: learned thresholds saved (sensitivity now custom)");
    } else {
        ESP_LOGE(TAG, "calibration done, but the learned thresholds could not be read back");
    }
    const rb_c4002_ack_t ack = make_ack(s_calib_request, RB_C4002_ACTION_CALIBRATE, result);
    send_ack(&ack);
}

static void remote_task(void *arg)
{
    for (;;) {
        rb_espnow_rx_t rx;
        if (xQueueReceive(s_queue, &rx, pdMS_TO_TICKS(500)) == pdTRUE) {
            handle_command(&rx);
        }
        if (s_calibrating && (int32_t)(now_ms() - s_calib_done_ms) >= 0 && c4002_calibration_remaining_s() == 0) {
            finish_calibration();
        }
    }
}

esp_err_t rb_c4002_remote_start(void)
{
#if CONFIG_RB_C4002_REMOTE_TUNING
    rb_node_link_controller_mac(s_controller);
    s_queue = xQueueCreate(RX_QUEUE_LEN, sizeof(rb_espnow_rx_t));
    ESP_RETURN_ON_FALSE(s_queue != NULL, ESP_ERR_NO_MEM, TAG, "queue");
    ESP_RETURN_ON_ERROR(rb_espnow_start_receiver(s_queue), TAG, "ESP-NOW receiver");
    ESP_RETURN_ON_FALSE(xTaskCreate(remote_task, "c4002_tuning", REMOTE_TASK_STACK, NULL, REMOTE_TASK_PRIO, NULL) ==
                            pdPASS,
                        ESP_ERR_NO_MEM, TAG, "task");
    ESP_LOGI(TAG, "remote C4002 tuning enabled (commands from " MACSTR ")", MAC2STR(s_controller));
#else
    ESP_LOGI(TAG, "remote C4002 tuning disabled in menuconfig");
#endif
    return ESP_OK;
}
