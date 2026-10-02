#pragma once

/*
 * Remote C4002 tuning, presence node side (docs/c4002_tuning.md).
 *
 * Listens for C4002_CONFIG packets from the controller and runs them in its
 * own task, because a full settings push takes about a dozen sensor commands
 * (up to a few seconds). Every command is answered with a C4002_CONFIG_ACK
 * carrying the settings now in use. After a calibration finishes, the node
 * reads the learned gate thresholds back, saves them and sends another ACK.
 */
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Call after rb_node_link_start() and rb_node_c4002_start(). */
esp_err_t rb_c4002_remote_start(void);

#ifdef __cplusplus
}
#endif
