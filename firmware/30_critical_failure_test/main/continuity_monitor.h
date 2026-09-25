#pragma once

#include "esp_err.h"

/*
 * Watches the safety path while the telemetry path is down and reports:
 *
 *   [CRITICAL] MQTT DOWN: safety loop alive (last iteration 37 ms ago, +98 loops/s), state WARNING
 *   [CRITICAL] outage 41 s: max safety-loop gap 112 ms, 3 transitions, mqtt fault reported, state kept -> PASS
 *
 * Runs at priority 1 (below everything), so it can't disturb what it measures.
 */
esp_err_t continuity_monitor_start(void);
