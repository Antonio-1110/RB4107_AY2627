#pragma once

/* TCA9554 8-bit I2C GPIO expander (drives the Waveshare board's 8 relays). */
#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tca9554 *tca9554_handle_t;

/*
 * Attach to the expander and make output_mask pins outputs. The output
 * register is written with initial_output BEFORE the pins are switched to
 * outputs, so no relay can click during boot.
 */
esp_err_t tca9554_create(i2c_master_bus_handle_t bus, uint8_t address, uint8_t initial_output, uint8_t output_mask,
                         tca9554_handle_t *out);

/* Set one output pin (0-7). */
esp_err_t tca9554_set_pin(tca9554_handle_t dev, uint8_t pin, bool level);

/* Read back the output and configuration registers from the chip. */
esp_err_t tca9554_read_back(tca9554_handle_t dev, uint8_t *output, uint8_t *config);

/* Rewrite the cached output and direction registers (after a chip reset). */
esp_err_t tca9554_restore(tca9554_handle_t dev);

/* Output register value the driver believes it last wrote. */
uint8_t tca9554_cached_output(tca9554_handle_t dev);

#ifdef __cplusplus
}
#endif
