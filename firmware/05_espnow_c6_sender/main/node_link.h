#pragma once

#include "esp_err.h"

/*
 * Start the ESP-NOW link task. It sends:
 *  - SENSOR_DATA every CONFIG_RB_NODE_DATA_PERIOD_MS,
 *  - HEARTBEAT every CONFIG_RB_NODE_HEARTBEAT_PERIOD_MS,
 *  - SENSOR_FAULT right away whenever a sensor fault flag changes.
 */
esp_err_t node_link_start(void);
