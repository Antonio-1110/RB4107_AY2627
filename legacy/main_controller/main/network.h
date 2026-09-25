#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t network_init(void);
bool network_is_connected(void);