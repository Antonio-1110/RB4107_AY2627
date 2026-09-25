#pragma once

/*
 * RB4107 logging conventions (TODO section 25, docs/logging.md).
 *
 * - One subsystem tag per module: SAFETY, ESPNOW, SENSOR, THERMAL, OUTPUT,
 *   MQTT, NET, FAULT, ... ESP-IDF prints them as "W (1234) SAFETY: ...".
 * - Log state changes and periodic summaries, never every iteration of a
 *   fast loop. RB_LOG_EVERY_MS() rate-limits a line that could repeat.
 * - Verbosity: CONFIG_LOG_DEFAULT_LEVEL globally, RB_LOG_DEBUG_TAGS /
 *   RB_LOG_QUIET_TAGS per tag at boot, or `log <tag> <level>` in the
 *   diagnostic console (project 27).
 */
#include <stdint.h>
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Apply the per-tag levels from menuconfig. Call early in app_main(). */
void rb_log_init(void);

/* True at most once per interval_ms for the given call site's state variable. */
static inline int rb_log_due(uint32_t *last_ms, uint32_t interval_ms)
{
    const uint32_t now = esp_log_timestamp();
    if (*last_ms != 0 && (uint32_t)(now - *last_ms) < interval_ms) {
        return 0;
    }
    *last_ms = now == 0 ? 1 : now;
    return 1;
}

/* Rate-limited log line: RB_LOG_EVERY_MS(5000, ESP_LOGW, TAG, "queue overflow %d", n); */
#define RB_LOG_EVERY_MS(interval_ms, log_macro, tag, ...)          \
    do {                                                           \
        static uint32_t rb_log_last_ms_;                           \
        if (rb_log_due(&rb_log_last_ms_, (interval_ms))) {         \
            log_macro(tag, __VA_ARGS__);                           \
        }                                                          \
    } while (0)

#ifdef __cplusplus
}
#endif
