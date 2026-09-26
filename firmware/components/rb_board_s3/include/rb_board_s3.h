#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create the board I2C bus once; later calls return the same handle. */
esp_err_t rb_board_i2c_bus(i2c_master_bus_handle_t *out);

/* Probe every 7-bit address and log the ones that answer. Returns the count. */
int rb_board_i2c_scan(void);

#ifdef __cplusplus
}
#endif
