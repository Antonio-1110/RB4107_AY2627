#pragma once

/*
 * Wall-clock time (TODO section 15): event timestamps, logs and MQTT
 * messages ONLY. Safety timing uses rb_time_mono_ms() and never this.
 *
 * Sources, in order: the PCF85063 RTC at boot, then SNTP once the network
 * is up (every sync is written back to the RTC).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Read the RTC and set the system clock from it if the RTC holds a valid time. */
esp_err_t rb_wallclock_init(void);

/* Start SNTP (call after the network is up). Does nothing if no server is configured. */
esp_err_t rb_wallclock_start_sntp(void);

/* True once the wall clock came from the RTC or SNTP. */
bool rb_wallclock_valid(void);

/* Set the wall clock (UTC) and store it in the RTC. */
esp_err_t rb_wallclock_set(time_t utc);

/*
 * Format the wall time of a monotonic timestamp (rb_time_mono_ms() value)
 * as ISO 8601 with offset, e.g. "2026-09-26T00:00:00+08:00". Returns false
 * (and writes "") while the wall clock is not valid.
 */
bool rb_wallclock_iso8601(uint32_t mono_ms, char *buf, size_t len);

#ifdef __cplusplus
}
#endif
