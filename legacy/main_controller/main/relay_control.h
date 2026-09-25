#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t relay_init(void);
esp_err_t relay_set(uint8_t relay, bool state);
bool relay_get(uint8_t relay);
void relay_all_off(void);