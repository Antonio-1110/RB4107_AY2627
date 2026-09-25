/*
 * TODO section 22: MQTT publishing strategy.
 *
 * The controller runtime plus the telemetry task, with the sensor node
 * simulated by default (RB_SIM_NODE) so every publishing path can be seen
 * without hardware. The scenario loops through heating, leaving (WARNING,
 * SHUTDOWN), returning and resetting, an MLX90640 fault, and the node going
 * offline and coming back.
 *
 * Watch on the MacBook:  tools/mqtt/watch.sh
 * Routine telemetry arrives every RB_MQTT_TELEMETRY_PERIOD_MS (QoS 0);
 * transitions, warnings, shutdowns and faults arrive immediately (QoS 1).
 */
#include "esp_log.h"
#include "fault_manager.h"
#include "rb_app_faults.h"
#include "rb_config.h"
#include "rb_connectivity.h"
#include "rb_controller.h"
#include "rb_espnow.h"
#include "rb_outputs.h"
#include "rb_sim.h"
#include "rb_telemetry.h"
#include "rb_wallclock.h"

static const char *TAG = "APP";

#define COLD 25.0f
#define HOT 120.0f

/* {node_online, presence_valid, person_present, thermal_valid, hot_region_c, rate_c_per_min} */
static const rb_sim_step_t SCENARIO[] = {
    {0, "cook present, hob cold", {true, true, true, true, COLD, 0}, false},
    {5000, "hob heating up", {true, true, true, true, HOT, 8}, false},
    {10000, "cook leaves", {true, true, false, true, HOT, 2}, false},
    {110000, "cook returns and presses reset", {true, true, true, true, HOT, 0}, true},
    {120000, "MLX90640 fails", {true, true, true, false, HOT, 0}, false},
    {130000, "MLX90640 recovers", {true, true, true, true, HOT, 0}, false},
    {140000, "sensor node disappears", {false, true, true, true, HOT, 0}, false},
    {160000, "sensor node back, hob off", {true, true, true, true, COLD, -5}, false},
    {175000, "loop", {true, true, true, true, COLD, 0}, false},
};

static void apply_outputs(const safety_outputs_t *out, void *ctx)
{
    rb_outputs_apply(out);
}

void app_main(void)
{
    const rb_controller_config_t cfg = rb_controller_config_from_kconfig();
    ESP_ERROR_CHECK(rb_controller_init(&cfg)); /* queues first: boot faults are events too */
    rb_app_faults_init();
    if (rb_wallclock_init() != ESP_OK) {
        fault_raise(FAULT_RTC, 0);
    }
    rb_outputs_init();

    const rb_controller_hooks_t hooks = {
        .apply_outputs = apply_outputs,
        .tick = rb_app_faults_tick,
        .self_test = rb_app_self_test,
        .safety_fault_active = rb_app_faults_safety_active,
        .node_events = rb_app_faults_node_events,
    };
    ESP_ERROR_CHECK(rb_controller_start(&cfg, &hooks));

#if CONFIG_RB_SIM_NODE
    ESP_ERROR_CHECK(rb_sim_start(CONFIG_RB_CTRL_NODE_ID, 500));
    ESP_ERROR_CHECK(rb_sim_run_script(SCENARIO, sizeof(SCENARIO) / sizeof(SCENARIO[0]), true));
#else
    ESP_ERROR_CHECK(rb_espnow_start(CONFIG_RB_ESPNOW_CHANNEL));
    ESP_ERROR_CHECK(rb_espnow_start_receiver(rb_controller_rx_queue()));
#endif

    /* Telemetry path last, and nothing above waits for it. */
    ESP_ERROR_CHECK(rb_telemetry_start());
    if (rb_connectivity_start() != ESP_OK) {
        ESP_LOGE(TAG, "network not started; safety continues without telemetry");
    }
}
