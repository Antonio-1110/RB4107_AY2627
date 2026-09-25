/*
 * TODO section 12: FreeRTOS architecture of the controller.
 *
 *   ESP-NOW callback -> sensor queue -> safety task (prio 12) -> state machine -> outputs
 *                                             '-> event queue -> telemetry task (prio 3)
 *
 * The telemetry task here is a stand-in for MQTT (section 19). To show the
 * critical requirement ("MQTT stalls -> safety task MUST continue"), it
 * deliberately hangs for 8 s every 30 s. The safety loop counter in the log
 * keeps climbing through each stall, and the event queue absorbs the
 * transitions that happen meanwhile (or counts them as dropped).
 *
 * Outputs are only logged here; the buzzer and relay drivers come in
 * sections 13 and 14.
 */
#include <inttypes.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rb_config.h"
#include "rb_controller.h"
#include "rb_espnow.h"

static const char *TAG = "TELEMETRY";

#define STALL_EVERY_MS 30000
#define STALL_FOR_MS 8000

static void apply_outputs(const safety_outputs_t *out, void *ctx)
{
    static safety_outputs_t last = {.buzzer = -1};
    if (out->buzzer != last.buzzer || out->shutdown != last.shutdown) {
        ESP_LOGW("OUTPUT", "buzzer=%s shutdown=%s", safety_buzzer_name(out->buzzer), out->shutdown ? "ACTIVE" : "released");
        last = *out;
    }
}

/* Stand-in for the MQTT task: lower priority, and it stalls on purpose. */
static void telemetry_task(void *arg)
{
    TickType_t next_stall = xTaskGetTickCount() + pdMS_TO_TICKS(STALL_EVERY_MS);
    for (;;) {
        rb_event_t evt;
        while (rb_controller_next_event(&evt, pdMS_TO_TICKS(1000))) {
            if (evt.type == RB_EVT_STATE_CHANGE) {
                ESP_LOGI(TAG, "event: state %s -> %s", safety_state_name(evt.state.from), safety_state_name(evt.state.to));
            } else if (evt.type == RB_EVT_NODE) {
                ESP_LOGI(TAG, "event: node %" PRIu32 " events 0x%02" PRIx32, evt.node.node_id, evt.node.events);
            }
        }
        rb_snapshot_t snap;
        rb_controller_get_snapshot(&snap);
        ESP_LOGI(TAG, "state=%s node=%s safety loops=%" PRIu32 " (last %" PRIu32 " ms ago) events dropped=%" PRIu32,
                 safety_state_name(snap.state), node_link_state_name(snap.node.link), snap.loop_count,
                 snap.mono_ms ? (uint32_t)(esp_log_timestamp() - snap.last_loop_ms) : 0, snap.events_dropped);

        if (xTaskGetTickCount() >= next_stall) {
            next_stall = xTaskGetTickCount() + pdMS_TO_TICKS(STALL_EVERY_MS);
            ESP_LOGW(TAG, "simulating an MQTT stall for %d ms...", STALL_FOR_MS);
            vTaskDelay(pdMS_TO_TICKS(STALL_FOR_MS)); /* the safety task keeps running meanwhile */
            ESP_LOGW(TAG, "stall over");
        }
    }
}

void app_main(void)
{
    const rb_controller_config_t cfg = rb_controller_config_from_kconfig();
    const rb_controller_hooks_t hooks = {.apply_outputs = apply_outputs};
    ESP_ERROR_CHECK(rb_controller_start(&cfg, &hooks));

    ESP_ERROR_CHECK(rb_espnow_start(CONFIG_RB_ESPNOW_CHANNEL));
    ESP_ERROR_CHECK(rb_espnow_start_receiver(rb_controller_rx_queue()));

    xTaskCreate(telemetry_task, "telemetry", 4096, NULL, CONFIG_RB_CTRL_TELEMETRY_TASK_PRIO, NULL);
}
