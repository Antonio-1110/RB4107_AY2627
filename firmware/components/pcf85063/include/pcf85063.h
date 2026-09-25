#pragma once

/* NXP PCF85063 real-time clock (Waveshare board, I2C 0x51). Stores UTC. */
#include <stdbool.h>
#include <time.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t pcf85063_init(i2c_master_bus_handle_t bus, uint8_t address);

/*
 * Read the time as UTC seconds. Returns ESP_ERR_INVALID_STATE when the
 * oscillator-stop flag says the time can't be trusted (first power-up, or
 * the backup battery ran flat).
 */
esp_err_t pcf85063_read(time_t *utc);

/* Write UTC seconds; also clears the oscillator-stop flag. */
esp_err_t pcf85063_write(time_t utc);

#ifdef __cplusplus
}
#endif
