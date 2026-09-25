#pragma once

#include <time.h>
#include "esp_err.h"

esp_err_t app_rtc_init(void);
time_t rtc_get_timestamp(void);
esp_err_t rtc_set_time(time_t timestamp);