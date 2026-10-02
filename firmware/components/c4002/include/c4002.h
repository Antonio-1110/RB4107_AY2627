#pragma once

/*
 * DFRobot C4002 ESP-IDF UART driver.
 *
 * A reader task parses every byte from the UART. Detection results are cached
 * with a timestamp; command responses are handed back to the caller of
 * c4002_command(). A reading goes invalid if no result arrives within
 * stale_timeout_ms, so a dead or unplugged sensor never looks like "nobody here".
 */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "c4002_proto.h"
#include "rb_c4002_params.h"
#include "rb_sensor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int uart_port;
    int rx_gpio;              /* ESP32 RX  <- C4002 TX */
    int tx_gpio;              /* ESP32 TX  -> C4002 RX */
    int out_gpio;             /* optional C4002 OUT pin, -1 if not wired */
    uint32_t baud;
    uint32_t stale_timeout_ms;
} c4002_config_t;

typedef enum {
    C4002_GATE_MOTION = 0x00,
    C4002_GATE_PRESENCE = 0x01,
} c4002_gate_type_t;

/*
 * Sensor-side detection parameters. The same struct travels over ESP-NOW
 * when the dashboard tunes the sensor (rb_c4002_params.h).
 */
typedef rb_c4002_params_t c4002_settings_t;

typedef struct {
    uint32_t frames_ok;
    uint32_t results;
    uint32_t checksum_errors;
    uint32_t length_errors;
    uint32_t invalid_results;
    uint32_t command_timeouts;
    uint32_t command_errors;
} c4002_stats_t;

esp_err_t c4002_init(const c4002_config_t *config);

/* Send one command and wait for its response. resp may be NULL. */
esp_err_t c4002_command(uint8_t frame_type, uint8_t cmd, const uint8_t *data, uint16_t data_len,
                        c4002_frame_t *resp, uint32_t timeout_ms);

/*
 * Push all detection settings to the sensor (LEDs on). Stops at the first
 * failing command. A CUSTOM sensitivity sends the per-gate thresholds instead
 * of a threshold group. Check them with rb_c4002_params_check() first.
 */
esp_err_t c4002_apply_settings(const c4002_settings_t *settings);

/*
 * Ask the sensor to learn its background noise and set its gate thresholds
 * from it. Keep the area empty. Runs inside the sensor; returns at once.
 */
esp_err_t c4002_start_env_calibration(uint16_t delay_s, uint16_t duration_s);

/* Seconds left in a running calibration, from the sensor's countdown notifications (0 = none). */
uint16_t c4002_calibration_remaining_s(void);

/*
 * Read the thresholds the sensor is using now (after a calibration, these are
 * the learned ones). gate_count is 15 or 25, matching the current resolution.
 */
esp_err_t c4002_read_gate_thresholds(c4002_gate_type_t type, unsigned gate_count, uint8_t *thresholds);

/* Latest reading. Returns out->valid. */
bool c4002_get_reading(presence_reading_t *out);

/* Latest raw result, for diagnostics. Returns false if none received yet. */
bool c4002_get_raw(c4002_result_t *out, uint32_t *age_ms);

/* Level of the OUT pin, or -1 if not wired. For cross-checking the UART reports. */
int c4002_get_out_level(void);

void c4002_get_stats(c4002_stats_t *out);

#ifdef __cplusplus
}
#endif
