#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct
{
    float temperature;
    bool presence_detected;
    float distance;
    int64_t timestamp_us;
    bool temperature_valid;
    bool presence_valid;
    bool distance_valid;
    bool sensor_node_connected;
} sensor_data_t;

esp_err_t sensor_manager_init(void);
sensor_data_t sensor_manager_get_latest(void);
bool sensor_manager_is_stale(const sensor_data_t *data);
esp_err_t sensor_manager_update(const sensor_data_t *data);