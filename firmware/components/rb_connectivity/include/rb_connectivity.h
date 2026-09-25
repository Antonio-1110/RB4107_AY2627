#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Start the network, then SNTP and the MQTT client once an IP address is
 * obtained. Network and MQTT state changes raise/clear the TELEMETRY faults
 * network_disconnected / mqtt_disconnected. Requires fault_manager_init()
 * first. Returns without blocking; reconnects are handled in the background.
 */
esp_err_t rb_connectivity_start(void);

#ifdef __cplusplus
}
#endif
