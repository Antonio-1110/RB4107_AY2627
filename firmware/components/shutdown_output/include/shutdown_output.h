#pragma once

/*
 * Shutdown output (TODO section 14): the relay that cuts power to the
 * appliance. The safety logic only calls activate/release. Polarity, relay
 * channel and boot state are configuration, and the raw I2C/GPIO access
 * stays inside this component.
 */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t channel;              /* relay 1-8 */
    bool relay_active_high;       /* TCA9554 level that energises the relay */
    bool energise_to_shut_down;   /* false: fail-safe wiring (de-energise = shut down) */
    bool boot_active;             /* start with the shutdown active */
} shutdown_output_config_t;

shutdown_output_config_t shutdown_output_config_from_kconfig(void);

/* Put every relay into the configured safe boot state. */
esp_err_t shutdown_output_init(const shutdown_output_config_t *config);

esp_err_t shutdown_activate(void);
esp_err_t shutdown_release(void);

/* Requested state. */
bool shutdown_is_active(void);

/* Read the relay output back from the expander and compare it with the requested state. */
esp_err_t shutdown_verify(void);

#ifdef __cplusplus
}
#endif
