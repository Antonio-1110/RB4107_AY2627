#pragma once

/*
 * Diagnostic console (TODO section 27). Type `help` at the rb4107> prompt.
 *
 *   status | presence | thermal | espnow | node | safety | faults | mqtt
 *   sim on|off|present|absent|presence-invalid|thermal-invalid|thermal-valid|node-off|node-on
 *   sim temp <degC> [rate]
 *   timers test|normal
 *   reset
 *   log <TAG|*> <none|error|warn|info|debug|verbose>
 *
 * Simulated inputs go through rb_sim, which injects protocol packets at the
 * ESP-NOW entry point. Test timers swap the numbers in the one state
 * machine. No safety logic is duplicated for diagnostics.
 */
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the REPL task (low priority) on the configured console. */
esp_err_t rb_diag_start(void);

/* Safety configuration with the accelerated diagnostic timers applied. */
#include "safety.h"
safety_config_t rb_diag_test_safety_config(void);

#ifdef __cplusplus
}
#endif
