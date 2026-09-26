#pragma once

#include "esp_err.h"
#include "system_state.h"

esp_err_t safety_controller_init(void);
system_state_t safety_controller_get_state(void);
void safety_controller_task(void *arg);