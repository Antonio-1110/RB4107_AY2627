/*
 * TODO section 16: fault manager.
 *
 * The controller (safety task + outputs) runs with every fault source wired
 * to the fault manager:
 *   node offline / C4002 / MLX90640  (node health events)     SAFETY
 *   shutdown relay write/verify, self-test                     SAFETY
 *   invalid ESP-NOW packets, degraded link                     TELEMETRY
 *   rx / event queue overflow                                  TELEMETRY
 *
 * A demo task raises and clears a TELEMETRY fault ("mqtt_disconnected")
 * every 15 s. The log shows the safety state doesn't change when it does,
 * while SAFETY faults (e.g. power off the C6) put the state machine into
 * FAULT.
 */
#include <inttypes.h>
#include "esp_log.h"
#include "fault_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rb_app_faults.h"
#include "rb_config.h"
#include "rb_controller.h"
#include "rb_espnow.h"
#include "rb_outputs.h"

static const char *TAG = "FAULT";

static void apply_outputs(const safety_outputs_t *out, void *ctx)
{
    rb_outputs_apply(out);
}

static void demo_task(void *arg)
{
    bool mqtt_down = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(15000));
        rb_snapshot_t before;
        rb_controller_get_snapshot(&before);
        mqtt_down = !mqtt_down;
        fault_set(FAULT_MQTT_DOWN, mqtt_down, -1);
        vTaskDelay(pdMS_TO_TICKS(500));
        rb_snapshot_t after;
        rb_controller_get_snapshot(&after);
        ESP_LOGI(TAG, "simulated MQTT %s: safety state %s -> %s (%s)", mqtt_down ? "DOWN" : "UP",
                 safety_state_name(before.state), safety_state_name(after.state),
                 before.transitions == after.transitions ? "unaffected" : "changed for another reason");

        ESP_LOGI(TAG, "active faults 0x%08" PRIx32 ", any SAFETY fault: %s", fault_active_mask(),
                 fault_any_safety_active() ? "yes" : "no");
        for (int id = 0; id < FAULT_COUNT; id++) {
            fault_status_t st;
            fault_get_status(id, &st);
            if (st.active) {
                const fault_info_t *info = fault_info(id);
                ESP_LOGI(TAG, "  %-24s %-9s raised %" PRIu32 "x, detail %" PRId32, info->name,
                         fault_class_name(info->fault_class), st.raise_count, st.detail);
            }
        }
    }
}

void app_main(void)
{
    rb_outputs_init();
    rb_app_faults_init();

    const rb_controller_config_t cfg = rb_controller_config_from_kconfig();
    const rb_controller_hooks_t hooks = {
        .apply_outputs = apply_outputs,
        .tick = rb_app_faults_tick,
        .self_test = rb_app_self_test,
        .safety_fault_active = rb_app_faults_safety_active,
        .node_events = rb_app_faults_node_events,
    };
    ESP_ERROR_CHECK(rb_controller_start(&cfg, &hooks));
    ESP_ERROR_CHECK(rb_espnow_start(CONFIG_RB_ESPNOW_CHANNEL));
    ESP_ERROR_CHECK(rb_espnow_start_receiver(rb_controller_rx_queue()));
    xTaskCreate(demo_task, "fault_demo", 4096, NULL, 2, NULL);
}
