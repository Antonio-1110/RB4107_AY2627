#include "data_manager.h"

#include "buzzer.h"
#include "esp_timer.h"
#include "mqtt_client_app.h"
#include "network.h"
#include "safety_controller.h"

system_data_t data_manager_get_snapshot(void)
{
    return (system_data_t){
        .sensors = sensor_manager_get_latest(),
        .state = safety_controller_get_state(),
        .buzzer_state = buzzer_is_on(),
        .ethernet_connected = network_is_connected(),
        .mqtt_connected = mqtt_is_connected(),
        .timestamp_us = esp_timer_get_time(),
    };
}