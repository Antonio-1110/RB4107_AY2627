#pragma once

/*
 * Hobby servo (SG90, MG90S, MG996R...) on one GPIO. It sends the standard
 * 50 Hz pulse, whose width sets the angle: min_pulse_us at 0 degrees,
 * max_pulse_us at range_deg. The pulse keeps running after a move, so the
 * servo holds its position.
 *
 * Uses LEDC timer 0 / channel 0 in low-speed mode, which every ESP32 has.
 */
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int gpio;
    uint32_t min_pulse_us;   /* pulse at 0 degrees (typically 500-1000) */
    uint32_t max_pulse_us;   /* pulse at range_deg (typically 2000-2500) */
    uint32_t range_deg;      /* travel of the servo (typically 180) */
} servo_config_t;

/* Set up the PWM without moving the servo; the first servo_set_angle() starts the pulse. */
esp_err_t servo_init(const servo_config_t *config);

/* Move to angle_deg (0..range_deg). */
esp_err_t servo_set_angle(uint32_t angle_deg);

/* Pulse width for an angle, for logging. */
uint32_t servo_pulse_us(uint32_t angle_deg);

#ifdef __cplusplus
}
#endif
