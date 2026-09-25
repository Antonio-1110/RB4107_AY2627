#pragma once

/*
 * Glue between the safety state machine and the output drivers. The state
 * machine asks for "buzzer WARNING, shutdown on"; this component turns that
 * into buzzer patterns and relay commands, and periodically reads the relay
 * back to catch hardware faults.
 */
#include <stdbool.h>
#include "esp_err.h"
#include "safety.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise the buzzer and shutdown relay from menuconfig (safe boot state). */
esp_err_t rb_outputs_init(void);

/* Apply the requested outputs. Cheap when nothing changes; safe to call every tick. */
void rb_outputs_apply(const safety_outputs_t *outputs);

/* True if the relay failed to initialise, write, or verify: a safety-relevant fault. */
bool rb_outputs_fault(void);

#ifdef __cplusplus
}
#endif
