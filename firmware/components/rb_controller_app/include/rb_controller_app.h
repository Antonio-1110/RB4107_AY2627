#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Start the complete controller, in an order that keeps the safety path
 * independent of the telemetry path:
 *
 *   1. logging, controller queues, fault manager, RTC (failure = telemetry fault)
 *   2. outputs in their safe boot state
 *   3. safety task (state machine, node health, outputs, faults)
 *   4. sensor input: ESP-NOW receiver, or the simulated node (RB_SIM_NODE)
 *   5. telemetry task, then network/SNTP/MQTT (nothing above waits for these)
 *   6. diagnostic console (RB_DIAG_CONSOLE), continuity monitor (RB_DIAG_CONTINUITY_MONITOR)
 */
esp_err_t rb_controller_app_start(void);

#ifdef __cplusplus
}
#endif
