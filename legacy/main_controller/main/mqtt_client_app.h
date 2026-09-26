#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t mqtt_app_init(void);
esp_err_t mqtt_publish_sensor_data(const char *payload);
esp_err_t mqtt_publish_event(const char *payload);
bool mqtt_is_connected(void);