/*
 * Wireless valve node bring-up, controller side.
 *
 * Plays the safety task's part in a loop, so the valve node
 * (firmware/valve_node, wireless version) can be checked without sensors:
 *
 *   1. SAFE      20 s  reports "no shutdown" every 100 ms  -> valve opens
 *   2. SHUTDOWN  10 s  reports "shutdown"                  -> CLOSE, valve closes at once
 *   3. SAFE      20 s                                      -> valve reopens
 *   4. SILENT    10 s  stops reporting (a hung safety task) -> CLOSE, valve closes
 *
 * To check the valve node's own timeout, unplug this board while the valve is
 * open: it closes about 3 s later.
 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "rb_config.h"
#include "rb_espnow.h"
#include "rb_valve_link.h"

static const char *TAG = "TEST";

#define FEED_MS 100

typedef struct {
    const char *name;
    bool feed;
    bool shutdown;
    uint32_t duration_ms;
} phase_t;

static const phase_t PHASES[] = {
    {"SAFE", true, false, 20000},
    {"SHUTDOWN", true, true, 10000},
    {"SAFE", true, false, 20000},
    {"SILENT (safety task stopped)", false, false, 10000},
};

void app_main(void)
{
    QueueHandle_t rx = xQueueCreate(8, sizeof(rb_espnow_rx_t));
    ESP_ERROR_CHECK(rx != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(rb_espnow_start(CONFIG_RB_ESPNOW_CHANNEL));
    ESP_ERROR_CHECK(rb_espnow_start_receiver(rx));
    ESP_ERROR_CHECK(rb_valve_link_start());

    for (;;) {
        for (size_t i = 0; i < sizeof(PHASES) / sizeof(PHASES[0]); i++) {
            const phase_t *p = &PHASES[i];
            ESP_LOGW(TAG, "---- phase %u: %s for %lu s ----", (unsigned)(i + 1), p->name,
                     (unsigned long)(p->duration_ms / 1000));
            for (uint32_t t = 0; t < p->duration_ms; t += FEED_MS) {
                if (p->feed) {
                    rb_valve_link_set_shutdown(p->shutdown);
                }
                rb_espnow_rx_t item;
                while (xQueueReceive(rx, &item, 0) == pdTRUE) {
                    if (item.packet.role != RB_NODE_ROLE_VALVE || !rb_valve_link_on_packet(&item.packet, item.rx_ms)) {
                        ESP_LOGW(TAG, "ignored %s from node %lu", rb_msg_type_name(item.packet.type),
                                 (unsigned long)item.packet.node_id);
                    }
                }
                vTaskDelay(pdMS_TO_TICKS(FEED_MS));
            }
            rb_valve_status_t st;
            uint32_t age;
            if (rb_valve_link_get_status(&st, &age)) {
                ESP_LOGI(TAG, "valve reports %s, reason %u, flags 0x%02x (%lu ms ago)",
                         st.position == RB_VALVE_POS_OPEN ? "OPEN" : "CLOSED", (unsigned)st.reason, st.flags,
                         (unsigned long)age);
            } else {
                ESP_LOGW(TAG, "no VALVE_STATUS received yet");
            }
        }
    }
}
