#pragma once

/*
 * MQTT publishing strategy (TODO section 22).
 *
 * Periodic (every RB_MQTT_TELEMETRY_PERIOD_MS, QoS 0):
 *   controller/state (telemetry), controller/heartbeat,
 *   sensors/<node>/presence and /thermal.
 * Immediately, on an event from the safety task (QoS 1):
 *   state transition  -> controller/state
 *   enter WARNING     -> events/warning
 *   enter SHUTDOWN    -> events/shutdown
 *   enter FAULT       -> events/fault
 *   fault raised/cleared (incl. node offline/restored) -> events/fault + controller/faults
 *   node health change -> sensors/<node>/status
 *
 * Raw thermal frames are never published.
 */
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t periodic;
    uint32_t events;
    uint32_t publish_failures;   /* not accepted by the MQTT client (e.g. QoS 0 while disconnected) */
} rb_telemetry_stats_t;

/* Start the telemetry task (priority RB_CTRL_TELEMETRY_TASK_PRIO). */
esp_err_t rb_telemetry_start(void);

void rb_telemetry_get_stats(rb_telemetry_stats_t *out);

#ifdef __cplusplus
}
#endif
