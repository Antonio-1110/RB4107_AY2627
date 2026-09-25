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
    C4002_RESOLUTION_80CM = 0x00, /* 15 gates, up to ~11 m */
    C4002_RESOLUTION_20CM = 0x01, /* 25 gates, up to ~4.9 m */
} c4002_resolution_t;

typedef enum {
    C4002_SENS_LOW = 0x00,
    C4002_SENS_MID = 0x01,
    C4002_SENS_HIGH = 0x02,
} c4002_sensitivity_t;

typedef enum {
    C4002_GATE_MOTION = 0x00,
    C4002_GATE_PRESENCE = 0x01,
} c4002_gate_type_t;

/* Sensor-side detection parameters (TODO section 2: expose sensitivity/detection parameters). */
typedef struct {
    uint8_t report_period_ds;      /* result report period, 0.1 s units */
    uint16_t range_min_cm;
    uint16_t range_max_cm;         /* <= 1100 */
    c4002_resolution_t resolution;
    c4002_sensitivity_t motion_sensitivity;
    c4002_sensitivity_t presence_sensitivity;
    uint16_t disappear_delay_s;    /* how long a target is held after it disappears */
    bool run_led;
    bool out_led;
} c4002_settings_t;

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

/* Push all detection settings to the sensor. Stops at the first failing command. */
esp_err_t c4002_apply_settings(const c4002_settings_t *settings);

/* Ask the sensor to learn its background noise. Keep the area empty. */
esp_err_t c4002_start_env_calibration(uint16_t delay_s, uint16_t duration_s);

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
