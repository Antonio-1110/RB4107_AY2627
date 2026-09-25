/*
 * TODO section 30: critical failure test.
 *
 * The production controller (as in project 29) plus a continuity monitor
 * that proves, during an MQTT/MacBook outage, that:
 *   - the S3 reports the telemetry fault (mqtt_disconnected),
 *   - the safety loop never stalls, ESP-NOW keeps arriving, and the state
 *     machine keeps deciding and driving the buzzer/relay,
 *   - reconnecting does not reset the safety state.
 *
 * Run tools/diagnostics/critical_failure_test.py on the MacBook alongside.
 */
#include "continuity_monitor.h"
#include "esp_err.h"
#include "rb_controller_app.h"

void app_main(void)
{
    ESP_ERROR_CHECK(rb_controller_app_start());
    ESP_ERROR_CHECK(continuity_monitor_start());
}
