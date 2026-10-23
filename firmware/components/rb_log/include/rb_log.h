#pragma once

/*
 * RB4107 logging conventions (TODO section 25, docs/logging.md).
 *
 * - One subsystem tag per module: SAFETY, ESPNOW, SENSOR, THERMAL, OUTPUT,
 *   MQTT, NET, FAULT, ... ESP-IDF prints them as "W (1234) SAFETY: ...".
 * - Log state changes and periodic summaries, never every iteration of a
 *   fast loop. RB_LOG_EVERY_MS() rate-limits a line that could repeat.
 * - Verbosity: CONFIG_LOG_DEFAULT_LEVEL globally, RB_LOG_DEBUG_TAGS /
 *   RB_LOG_QUIET_TAGS per tag at boot, or at runtime with `log <tag> <level>`
 *   in the diagnostic console or the dashboard's Logs tab (firmware/controller).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Per-tag levels remembered by rb_log_set_level(), so they can be reported and reset. */
#define RB_LOG_TRACKED_TAGS 16
#define RB_LOG_TAG_LEN 16 /* including the NUL: tags of up to 15 characters */

typedef struct {
    char tag[RB_LOG_TAG_LEN];
    esp_log_level_t level;
} rb_log_tag_level_t;

/* Apply the per-tag levels from menuconfig. Call early in app_main(). */
void rb_log_init(void);

/* "none", "error", "warn", "info", "debug", "verbose". */
const char *rb_log_level_name(esp_log_level_t level);
/* The reverse of rb_log_level_name(). */
bool rb_log_level_parse(const char *name, esp_log_level_t *out);

/*
 * esp_log_level_set() that also remembers the level, for rb_log_levels().
 * tag "*" sets the default level and keeps the per-tag levels. Returns false
 * (level still applied) when the tag is too long or the table is full.
 */
bool rb_log_set_level(const char *tag, esp_log_level_t level);

/* Back to the menuconfig levels: CONFIG_LOG_DEFAULT_LEVEL, RB_LOG_DEBUG_TAGS and RB_LOG_QUIET_TAGS. */
void rb_log_reset_levels(void);

/* The default level and up to max per-tag levels in force; returns how many were written. */
size_t rb_log_levels(esp_log_level_t *default_level, rb_log_tag_level_t *out, size_t max);

/* Changes on every rb_log_set_level() / rb_log_reset_levels(). */
uint32_t rb_log_levels_version(void);

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
