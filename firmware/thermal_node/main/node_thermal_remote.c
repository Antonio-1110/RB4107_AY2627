#include "node_thermal_remote.h"

#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "node_thermal.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "rb_config.h"
#include "rb_espnow.h"
#include "rb_node_id.h"
#include "rb_node_link.h"
#include "rb_protocol.h"

static const char *TAG = "THERMAL_CFG";

#define NVS_NAMESPACE "rb_thermal"
#define NVS_KEY_HOT "hot_centi"
#define TASK_STACK 3072
#define TASK_PRIO 4 /* below the link task */
#define RX_QUEUE_LEN 4
/* The menuconfig range of RB_THERMAL_HOT_PIXEL_THRESHOLD_DC, in 0.01 degC. */
#define HOT_MIN_CENTI (-4000)
#define HOT_MAX_CENTI 30000

static QueueHandle_t s_queue;
static uint8_t s_controller[6];

static bool open_nvs(nvs_open_mode_t mode, nvs_handle_t *h)
{
    esp_err_t err = nvs_flash_init(); /* also done by Wi-Fi start; a second call is harmless */
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    return err == ESP_OK && nvs_open(NVS_NAMESPACE, mode, h) == ESP_OK;
}

void node_thermal_remote_load(void)
{
#if CONFIG_RB_THERMAL_REMOTE_THRESHOLD
    nvs_handle_t h;
    if (!open_nvs(NVS_READONLY, &h)) {
        return; /* nothing saved yet */
    }
    int16_t centi;
    if (nvs_get_i16(h, NVS_KEY_HOT, &centi) == ESP_OK && centi >= HOT_MIN_CENTI && centi <= HOT_MAX_CENTI) {
        node_thermal_set_hot_threshold(centi / 100.0f);
        ESP_LOGI(TAG, "hot-pixel threshold %.1f C (saved from the dashboard)", centi / 100.0f);
    }
    nvs_close(h);
#endif
}

static void save(bool erase, int16_t centi)
{
    nvs_handle_t h;
    if (!open_nvs(NVS_READWRITE, &h)) {
        ESP_LOGW(TAG, "NVS not available: the change lasts until the next reboot");
        return;
    }
    esp_err_t err = erase ? nvs_erase_key(h, NVS_KEY_HOT) : nvs_set_i16(h, NVS_KEY_HOT, centi);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK; /* nothing was saved */
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "not saved (%s): the change lasts until the next reboot", esp_err_to_name(err));
    }
}

static void handle(const rb_espnow_rx_t *rx)
{
    const rb_thermal_config_msg_t *cmd = &rx->packet.body.thermal_config;
    if (rx->packet.type != RB_MSG_THERMAL_CONFIG || memcmp(rx->src_mac, s_controller, 6) != 0) {
        ESP_LOGW(TAG, "ignored %s from " MACSTR " (not the controller)", rb_msg_type_name(rx->packet.type),
                 MAC2STR(rx->src_mac));
        return;
    }
    if (cmd->target_node_id != rb_node_id()) {
        ESP_LOGW(TAG, "ignored request %u for node %lu", cmd->request_id, (unsigned long)cmd->target_node_id);
        return;
    }
    switch (cmd->action) {
    case RB_THERMAL_ACTION_SET:
        if (cmd->hot_threshold_centi < HOT_MIN_CENTI || cmd->hot_threshold_centi > HOT_MAX_CENTI) {
            ESP_LOGW(TAG, "request %u rejected: threshold %d (0.01 C) out of range", cmd->request_id,
                     cmd->hot_threshold_centi);
            return;
        }
        node_thermal_set_hot_threshold(cmd->hot_threshold_centi / 100.0f);
        save(false, cmd->hot_threshold_centi);
        ESP_LOGI(TAG, "request %u: hot-pixel threshold now %.1f C", cmd->request_id,
                 cmd->hot_threshold_centi / 100.0f);
        break;
    case RB_THERMAL_ACTION_DEFAULTS:
        node_thermal_set_hot_threshold(CONFIG_RB_THERMAL_HOT_PIXEL_THRESHOLD_DC / 10.0f);
        save(true, 0);
        ESP_LOGI(TAG, "request %u: hot-pixel threshold back to the menuconfig %.1f C", cmd->request_id,
                 CONFIG_RB_THERMAL_HOT_PIXEL_THRESHOLD_DC / 10.0f);
        break;
    default:
        ESP_LOGW(TAG, "request %u: unknown action %u", cmd->request_id, cmd->action);
        break;
    }
}

static void remote_task(void *arg)
{
    for (;;) {
        rb_espnow_rx_t rx;
        if (xQueueReceive(s_queue, &rx, portMAX_DELAY) == pdTRUE) {
            handle(&rx);
        }
    }
}

esp_err_t node_thermal_remote_start(void)
{
#if CONFIG_RB_THERMAL_REMOTE_THRESHOLD
    rb_node_link_controller_mac(s_controller);
    s_queue = xQueueCreate(RX_QUEUE_LEN, sizeof(rb_espnow_rx_t));
    ESP_RETURN_ON_FALSE(s_queue != NULL, ESP_ERR_NO_MEM, TAG, "queue");
    ESP_RETURN_ON_ERROR(rb_espnow_start_receiver(s_queue), TAG, "ESP-NOW receiver");
    ESP_RETURN_ON_FALSE(xTaskCreate(remote_task, "thermal_cfg", TASK_STACK, NULL, TASK_PRIO, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "task");
    ESP_LOGI(TAG, "hot-pixel threshold from the dashboard enabled (commands from " MACSTR ")",
             MAC2STR(s_controller));
#else
    ESP_LOGI(TAG, "hot-pixel threshold from the dashboard disabled in menuconfig");
#endif
    return ESP_OK;
}
