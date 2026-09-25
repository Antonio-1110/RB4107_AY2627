#include "rb_controller_app.h"

#include "esp_check.h"
#include "esp_log.h"
#include "fault_manager.h"
#include "rb_app_faults.h"
#include "rb_config.h"
#include "rb_connectivity.h"
#include "rb_continuity.h"
#include "rb_controller.h"
#include "rb_diag.h"
#include "rb_espnow.h"
#include "rb_log.h"
#include "rb_outputs.h"
#include "rb_sim.h"
#include "rb_telemetry.h"
#include "rb_wallclock.h"

static const char *TAG = "CONTROLLER";

static void apply_outputs(const safety_outputs_t *out, void *ctx)
{
    rb_outputs_apply(out);
}

esp_err_t rb_controller_app_start(void)
{
    rb_log_init();
    ESP_LOGI(TAG, "RB4107 controller %s starting", CONFIG_RB_MQTT_CONTROLLER_ID);

    const rb_controller_config_t cfg = rb_controller_config_from_kconfig();
    ESP_RETURN_ON_ERROR(rb_controller_init(&cfg), TAG, "controller queues");
    rb_app_faults_init();
    if (rb_wallclock_init() != ESP_OK) {
        fault_raise(FAULT_RTC, 0); /* timestamps only: safety unaffected */
    }
    rb_outputs_init(); /* a relay failure becomes a SAFETY fault via the self-test / tick hooks */

    const rb_controller_hooks_t hooks = {
        .apply_outputs = apply_outputs,
        .tick = rb_app_faults_tick,
        .self_test = rb_app_self_test,
        .safety_fault_active = rb_app_faults_safety_active,
        .node_events = rb_app_faults_node_events,
    };
    ESP_RETURN_ON_ERROR(rb_controller_start(&cfg, &hooks), TAG, "safety task");
#if CONFIG_RB_DIAG_TEST_TIMERS_AT_BOOT
    const safety_config_t test = rb_diag_test_safety_config();
    rb_controller_set_safety_config(&test, true);
#endif

#if CONFIG_RB_SIM_NODE
    ESP_RETURN_ON_ERROR(rb_sim_start(CONFIG_RB_CTRL_NODE_ID, 500), TAG, "simulated node");
#else
    ESP_RETURN_ON_ERROR(rb_espnow_start(CONFIG_RB_ESPNOW_CHANNEL), TAG, "ESP-NOW");
    ESP_RETURN_ON_ERROR(rb_espnow_start_receiver(rb_controller_rx_queue()), TAG, "ESP-NOW receiver");
#endif

    /* Telemetry path: best effort, never fatal. */
    if (rb_telemetry_start() != ESP_OK) {
        ESP_LOGE(TAG, "telemetry task not started; safety unaffected");
    }
    if (rb_connectivity_start() != ESP_OK) {
        ESP_LOGE(TAG, "network not started; safety continues without telemetry");
    }

#if CONFIG_RB_DIAG_CONSOLE
    rb_diag_start();
#endif
#if CONFIG_RB_DIAG_CONTINUITY_MONITOR
    rb_continuity_start();
#endif
    return ESP_OK;
}
