#pragma once

#include "esp_err.h"

/*
 * Critical-failure continuity monitor (TODO section 30), enabled with
 * RB_DIAG_CONTINUITY_MONITOR and started by rb_controller_app.
 *
 * Watches the safety path while the telemetry path is down and reports:
 *
 *   [CRITICAL] MQTT DOWN: safety loop alive (last iteration 37 ms ago, +98 loops/s), state WARNING
 *   [CRITICAL] outage 41 s: max safety-loop gap 112 ms, 3 transitions, mqtt fault reported, state kept -> PASS
 *
 * Runs at priority 1 (below everything), so it can't disturb what it measures.
 */
esp_err_t rb_continuity_start(void);
