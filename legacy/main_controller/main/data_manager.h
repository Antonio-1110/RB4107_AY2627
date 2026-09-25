#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sensor_manager.h"
#include "system_state.h"

typedef struct
{
    sensor_data_t sensors;
    system_state_t state;
    bool buzzer_state;
    bool ethernet_connected;
    bool mqtt_connected;
    int64_t timestamp_us;
} system_data_t;

system_data_t data_manager_get_snapshot(void);