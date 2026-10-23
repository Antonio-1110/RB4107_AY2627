#pragma once

/*
 * The controller's log lines for the dashboard (docs/logging.md, "Logs tab").
 *
 * rb_log_capture_start() hooks ESP-IDF logging: every line still goes to the
 * serial port unchanged, and a copy goes into a ring buffer of
 * RB_MQTT_LOG_BUFFER_LINES lines (the oldest is overwritten and counted as
 * dropped). The telemetry task takes the lines and publishes them on
 * <prefix>/controller/log. Copying never waits: a line logged while another
 * task is being copied is dropped and counted, so logging can't hold up the
 * safety task. What the serial port shows is what the dashboard shows: the
 * per-tag levels (rb_log_set_level()) decide both.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RB_LOG_LINE_TAG_LEN 16
#define RB_LOG_LINE_MSG_LEN 120 /* longer messages are cut */

typedef struct {
    uint32_t uptime_ms;              /* when it was logged (esp_log_timestamp) */
    char level;                      /* 'E' 'W' 'I' 'D' 'V' */
    char tag[RB_LOG_LINE_TAG_LEN];   /* "-" when the line has no tag */
    char msg[RB_LOG_LINE_MSG_LEN];   /* printable ASCII only */
} rb_log_line_t;

/*
 * Parse one line as ESP-IDF prints it: "W (1234) SAFETY: text", with or
 * without colour codes and the trailing newline; the time in brackets can
 * also be a clock time. Returns false for an empty line. A line in another
 * shape is kept whole as an 'I' line with tag "-".
 */
bool rb_log_line_parse(const char *text, size_t len, uint32_t uptime_ms, rb_log_line_t *out);

/* Install the hook (once, early in the controller's start-up). */
esp_err_t rb_log_capture_start(void);

/* Move up to max of the oldest lines into out; returns how many. */
size_t rb_log_capture_take(rb_log_line_t *out, size_t max);

/* Lines lost so far: buffer full, or logged while another line was being copied. */
uint32_t rb_log_capture_dropped(void);

#ifdef __cplusplus
}
#endif
