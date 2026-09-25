#pragma once

/*
 * The two kinds of time in RB4107 (TODO sections 11 and 15).
 *
 * Monotonic time (this header) comes from esp_timer: microseconds since boot.
 * It never jumps, and only it may be used for safety timing (unattended,
 * warning and sensor timeouts, state durations).
 *
 * Wall-clock time (RTC / SNTP) is only for timestamps in logs and MQTT
 * messages. Safety logic must never depend on it being right.
 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Milliseconds since boot. Wraps after ~49 days; compare as (uint32_t)(now - then). */
uint32_t rb_time_mono_ms(void);

/* Microseconds since boot (64-bit, does not wrap in practice). */
int64_t rb_time_mono_us(void);

/* True once timeout_ms has passed since since_ms (wrap-safe). */
static inline int rb_time_elapsed(uint32_t now_ms, uint32_t since_ms, uint32_t timeout_ms)
{
    return (uint32_t)(now_ms - since_ms) >= timeout_ms;
}

#ifdef __cplusplus
}
#endif
