#include "rb_ctrl_cmd.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define KEY_LEN 24
#define STR_LEN 40

typedef struct {
    const char *p;
    const char *end;
} scan_t;

static void skip_ws(scan_t *s)
{
    while (s->p < s->end && (*s->p == ' ' || *s->p == '\t' || *s->p == '\n' || *s->p == '\r')) {
        s->p++;
    }
}

static bool eat(scan_t *s, char c)
{
    skip_ws(s);
    if (s->p < s->end && *s->p == c) {
        s->p++;
        return true;
    }
    return false;
}

/* Names and short words only: escapes are rejected. */
static bool read_string(scan_t *s, char *out, size_t cap)
{
    if (!eat(s, '"')) {
        return false;
    }
    size_t n = 0;
    while (s->p < s->end && *s->p != '"') {
        if (*s->p == '\\' || (unsigned char)*s->p < 0x20 || n + 1 >= cap) {
            return false;
        }
        out[n++] = *s->p++;
    }
    out[n] = '\0';
    return eat(s, '"');
}

/* Non-negative integers only. */
static bool read_uint(scan_t *s, long *out)
{
    skip_ws(s);
    const char *start = s->p;
    long v = 0;
    while (s->p < s->end && *s->p >= '0' && *s->p <= '9') {
        if (v > 1000000) {
            return false;
        }
        v = v * 10 + (*s->p++ - '0');
    }
    if (s->p == start || (s->p < s->end && (*s->p == '.' || *s->p == 'e' || *s->p == 'E'))) {
        return false;
    }
    *out = v;
    return true;
}

static void fail(char *err, size_t len, const char *fmt, ...)
{
    if (err == NULL || len == 0) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, len, fmt, ap);
    va_end(ap);
}

enum {
    SEEN_VERSION = 1,
    SEEN_TYPE = 2,
    SEEN_ID = 4,
    SEEN_REQUEST = 8,
    SEEN_ACTION = 16,
    SEEN_ALL = 31,
    SEEN_ABSENCE = 32,
    SEEN_RETURN = 64,
    SEEN_GAP = 128,
    SEEN_WARNING = 256,
    SEEN_SHUTDOWN = 512,
    SEEN_FILTER = SEEN_ABSENCE | SEEN_RETURN | SEEN_GAP | SEEN_WARNING | SEEN_SHUTDOWN,
    SEEN_COOKING = 1024,
    SEEN_COOKWARE = 2048,
    SEEN_T1_TEMP = 1 << 12,
    SEEN_T1_WARN = 1 << 13,
    SEEN_T1_STOP = 1 << 14,
    SEEN_T2_TEMP = 1 << 15,
    SEEN_T2_RATE = 1 << 16,
    SEEN_T2_RATE_MIN = 1 << 17,
    SEEN_T2_WARN = 1 << 18,
    SEEN_T2_STOP = 1 << 19,
    SEEN_PAUSE_MS = 1 << 20,
    SEEN_PAUSE_PCT = 1 << 21,
    SEEN_FUSION = 1 << 22,
    SEEN_ATTENDANCE = SEEN_PAUSE_MS | SEEN_PAUSE_PCT | SEEN_FUSION,
    SEEN_R_MODE = 1 << 23,
    SEEN_R_GAP = 1 << 24,
    SEEN_R_DWELL = 1 << 25,
    SEEN_R_STILL = 1 << 26,
    SEEN_R_WINDOW = 1 << 27,
    SEEN_R_SPAN = 1 << 28,
    SEEN_R_ZONE = 1 << 29,
    SEEN_RADAR = SEEN_R_MODE | SEEN_R_GAP | SEEN_R_DWELL | SEEN_R_STILL | SEEN_R_WINDOW | SEEN_R_SPAN | SEEN_R_ZONE,
    SEEN_TIERS = SEEN_T1_TEMP | SEEN_T1_WARN | SEEN_T1_STOP | SEEN_T2_TEMP | SEEN_T2_RATE | SEEN_T2_RATE_MIN | SEEN_T2_WARN |
                 SEEN_T2_STOP,
};

/* One of the filter values: an integer min..max milliseconds. */
static bool filter_value(const char *key, bool is_str, long num, long min, long max, uint32_t *out, char *err,
                         size_t err_len)
{
    if (is_str || num < min || num > max) {
        fail(err, err_len, "%s must be an integer %ld..%ld", key, min, max);
        return false;
    }
    *out = (uint32_t)num;
    return true;
}

bool rb_ctrl_cmd_parse(const char *json, size_t len, rb_ctrl_cmd_t *out, char *err, size_t err_len)
{
    *out = (rb_ctrl_cmd_t){0};
    if (err != NULL && err_len > 0) {
        err[0] = '\0';
    }
    scan_t s = {.p = json, .end = json + len};
    if (!eat(&s, '{')) {
        fail(err, err_len, "not a JSON object");
        return false;
    }
    unsigned seen = 0;
    if (!eat(&s, '}')) {
        do {
            char key[KEY_LEN];
            if (!read_string(&s, key, sizeof(key)) || !eat(&s, ':')) {
                fail(err, err_len, "malformed JSON");
                return false;
            }
            skip_ws(&s);
            const bool is_str = s.p < s.end && *s.p == '"';
            char str[STR_LEN] = "";
            long num = 0;
            if (is_str ? !read_string(&s, str, sizeof(str)) : !read_uint(&s, &num)) {
                fail(err, err_len, "bad value for '%s'", key);
                return false;
            }
            unsigned bit;
            if (strcmp(key, "schema_version") == 0) {
                bit = SEEN_VERSION;
                if (is_str || num != 2) {
                    fail(err, err_len, "schema_version must be 2");
                    return false;
                }
            } else if (strcmp(key, "type") == 0) {
                bit = SEEN_TYPE;
                if (!is_str || strcmp(str, "controller_command") != 0) {
                    fail(err, err_len, "type must be \"controller_command\"");
                    return false;
                }
            } else if (strcmp(key, "controller_id") == 0) {
                bit = SEEN_ID;
                if (!is_str || str[0] == '\0') {
                    fail(err, err_len, "controller_id must be a non-empty string");
                    return false;
                }
                snprintf(out->controller_id, sizeof(out->controller_id), "%s", str);
            } else if (strcmp(key, "request_id") == 0) {
                bit = SEEN_REQUEST;
                if (is_str || num < 1 || num > 65535) {
                    fail(err, err_len, "request_id must be an integer 1..65535");
                    return false;
                }
                out->request_id = (uint16_t)num;
            } else if (strcmp(key, "action") == 0) {
                bit = SEEN_ACTION;
                if (is_str && strcmp(str, "reset") == 0) {
                    out->action = RB_CTRL_ACTION_RESET;
                } else if (is_str && strcmp(str, "presence_filter") == 0) {
                    out->action = RB_CTRL_ACTION_PRESENCE_FILTER;
                } else if (is_str && strcmp(str, "presence_filter_defaults") == 0) {
                    out->action = RB_CTRL_ACTION_PRESENCE_FILTER_DEFAULTS;
                } else if (is_str && strcmp(str, "cooking_temp") == 0) {
                    out->action = RB_CTRL_ACTION_COOKING_TEMP;
                } else if (is_str && strcmp(str, "cooking_temp_defaults") == 0) {
                    out->action = RB_CTRL_ACTION_COOKING_TEMP_DEFAULTS;
                } else if (is_str && strcmp(str, "cookware") == 0) {
                    out->action = RB_CTRL_ACTION_COOKWARE;
                } else if (is_str && strcmp(str, "cookware_defaults") == 0) {
                    out->action = RB_CTRL_ACTION_COOKWARE_DEFAULTS;
                } else if (is_str && strcmp(str, "attendance") == 0) {
                    out->action = RB_CTRL_ACTION_ATTENDANCE;
                } else if (is_str && strcmp(str, "attendance_defaults") == 0) {
                    out->action = RB_CTRL_ACTION_ATTENDANCE_DEFAULTS;
                } else if (is_str && strcmp(str, "radar_rules") == 0) {
                    out->action = RB_CTRL_ACTION_RADAR_RULES;
                } else if (is_str && strcmp(str, "radar_rules_defaults") == 0) {
                    out->action = RB_CTRL_ACTION_RADAR_RULES_DEFAULTS;
                } else if (is_str && strcmp(str, "tiers") == 0) {
                    out->action = RB_CTRL_ACTION_TIERS;
                } else if (is_str && strcmp(str, "tiers_defaults") == 0) {
                    out->action = RB_CTRL_ACTION_TIERS_DEFAULTS;
                } else {
                    fail(err, err_len,
                         "action must be reset, presence_filter, presence_filter_defaults, cooking_temp, "
                         "cooking_temp_defaults, cookware, cookware_defaults, tiers, tiers_defaults, attendance, "
                         "attendance_defaults, radar_rules or radar_rules_defaults");
                    return false;
                }
            } else if (strcmp(key, "absence_ms") == 0) {
                bit = SEEN_ABSENCE;
                if (!filter_value(key, is_str, num, 0, RB_CTRL_ABSENCE_MAX_MS, &out->absence_ms, err, err_len)) {
                    return false;
                }
            } else if (strcmp(key, "return_ms") == 0) {
                bit = SEEN_RETURN;
                if (!filter_value(key, is_str, num, 0, RB_CTRL_RETURN_MAX_MS, &out->return_ms, err, err_len)) {
                    return false;
                }
            } else if (strcmp(key, "return_gap_ms") == 0) {
                bit = SEEN_GAP;
                if (!filter_value(key, is_str, num, 0, RB_CTRL_RETURN_GAP_MAX_MS, &out->return_gap_ms, err, err_len)) {
                    return false;
                }
            } else if (strcmp(key, "warning_ms") == 0) {
                bit = SEEN_WARNING;
                if (!filter_value(key, is_str, num, RB_CTRL_TIMEOUT_MIN_MS, RB_CTRL_TIMEOUT_MAX_MS, &out->warning_ms, err,
                                  err_len)) {
                    return false;
                }
            } else if (strcmp(key, "shutdown_ms") == 0) {
                bit = SEEN_SHUTDOWN;
                if (!filter_value(key, is_str, num, RB_CTRL_TIMEOUT_MIN_MS, RB_CTRL_TIMEOUT_MAX_MS, &out->shutdown_ms, err,
                                  err_len)) {
                    return false;
                }
            } else if (strcmp(key, "cooking_temp_dc") == 0) {
                bit = SEEN_COOKING;
                if (is_str || num > RB_CTRL_COOKING_TEMP_MAX_DC) {
                    fail(err, err_len, "%s must be an integer 0..%d", key, RB_CTRL_COOKING_TEMP_MAX_DC);
                    return false;
                }
                out->cooking_temp_dc = (uint16_t)num;
            } else if (strcmp(key, "tier1_temp_dc") == 0 || strcmp(key, "tier2_temp_dc") == 0 ||
                       strcmp(key, "tier2_rate_min_temp_dc") == 0) {
                const bool one = key[4] == '1';
                bit = one ? SEEN_T1_TEMP : (strcmp(key, "tier2_temp_dc") == 0 ? SEEN_T2_TEMP : SEEN_T2_RATE_MIN);
                if (is_str || num > RB_CTRL_COOKING_TEMP_MAX_DC) {
                    fail(err, err_len, "%s must be an integer 0..%d", key, RB_CTRL_COOKING_TEMP_MAX_DC);
                    return false;
                }
                if (one) {
                    out->tier1_temp_dc = (uint16_t)num;
                } else if (bit == SEEN_T2_TEMP) {
                    out->tier2_temp_dc = (uint16_t)num;
                } else {
                    out->tier2_rate_min_temp_dc = (uint16_t)num;
                }
            } else if (strcmp(key, "tier2_rate_dc_per_min") == 0) {
                bit = SEEN_T2_RATE;
                if (is_str || num > RB_CTRL_TIER_RATE_MAX_DC_PER_MIN) {
                    fail(err, err_len, "%s must be an integer 0..%d", key, RB_CTRL_TIER_RATE_MAX_DC_PER_MIN);
                    return false;
                }
                out->tier2_rate_dc_per_min = (uint16_t)num;
            } else if (strcmp(key, "pause_budget_ms") == 0) {
                bit = SEEN_PAUSE_MS;
                if (!filter_value(key, is_str, num, 0, 600000, &out->pause_budget_ms, err, err_len)) {
                    return false;
                }
            } else if (strcmp(key, "pause_budget_pct") == 0) {
                bit = SEEN_PAUSE_PCT;
                if (is_str || num > 100) {
                    fail(err, err_len, "%s must be an integer 0..100", key);
                    return false;
                }
                out->pause_budget_pct = (uint8_t)num;
            } else if (strcmp(key, "presence_fusion") == 0) {
                bit = SEEN_FUSION;
                if (!is_str || (strcmp(str, "all") != 0 && strcmp(str, "any") != 0)) {
                    fail(err, err_len, "presence_fusion must be all or any");
                    return false;
                }
                out->fusion_all = strcmp(str, "all") == 0;
            } else if (strcmp(key, "radar_mode") == 0) {
                bit = SEEN_R_MODE;
                if (!is_str || (strcmp(str, "off") != 0 && strcmp(str, "observe") != 0 && strcmp(str, "enforce") != 0)) {
                    fail(err, err_len, "radar_mode must be off, observe or enforce");
                    return false;
                }
                snprintf(out->radar_mode, sizeof(out->radar_mode), "%s", str);
            } else if (strcmp(key, "radar_quiet_gap_ms") == 0) {
                bit = SEEN_R_GAP;
                if (!filter_value(key, is_str, num, 0, 10000, &out->radar_quiet_gap_ms, err, err_len)) {
                    return false;
                }
            } else if (strcmp(key, "radar_dwell_ms") == 0) {
                bit = SEEN_R_DWELL;
                if (!filter_value(key, is_str, num, 0, 60000, &out->radar_dwell_ms, err, err_len)) {
                    return false;
                }
            } else if (strcmp(key, "radar_stationary_min_ms") == 0) {
                bit = SEEN_R_STILL;
                if (!filter_value(key, is_str, num, 0, 60000, &out->radar_stationary_min_ms, err, err_len)) {
                    return false;
                }
            } else if (strcmp(key, "radar_settle_window_ms") == 0) {
                bit = SEEN_R_WINDOW;
                if (!filter_value(key, is_str, num, 500, 60000, &out->radar_settle_window_ms, err, err_len)) {
                    return false;
                }
            } else if (strcmp(key, "radar_settle_span_cm") == 0) {
                bit = SEEN_R_SPAN;
                if (is_str || num < 0 || num > 1000) {
                    fail(err, err_len, "%s must be an integer 0..1000", key);
                    return false;
                }
                out->radar_settle_span_cm = (uint16_t)num;
            } else if (strcmp(key, "radar_zone_max_cm") == 0) {
                bit = SEEN_R_ZONE;
                if (is_str || num < 0 || num > 1200) {
                    fail(err, err_len, "%s must be an integer 0..1200", key);
                    return false;
                }
                out->radar_zone_max_cm = (uint16_t)num;
            } else if (strcmp(key, "tier1_warning_ms") == 0) {
                bit = SEEN_T1_WARN;
                if (!filter_value(key, is_str, num, RB_CTRL_TIMEOUT_MIN_MS, RB_CTRL_TIMEOUT_MAX_MS, &out->tier1_warning_ms, err, err_len)) {
                    return false;
                }
            } else if (strcmp(key, "tier1_shutdown_ms") == 0) {
                bit = SEEN_T1_STOP;
                if (!filter_value(key, is_str, num, RB_CTRL_TIMEOUT_MIN_MS, RB_CTRL_TIMEOUT_MAX_MS, &out->tier1_shutdown_ms, err, err_len)) {
                    return false;
                }
            } else if (strcmp(key, "tier2_warning_ms") == 0) {
                bit = SEEN_T2_WARN;
                if (!filter_value(key, is_str, num, RB_CTRL_TIMEOUT_MIN_MS, RB_CTRL_TIMEOUT_MAX_MS, &out->tier2_warning_ms, err, err_len)) {
                    return false;
                }
            } else if (strcmp(key, "tier2_shutdown_ms") == 0) {
                bit = SEEN_T2_STOP;
                if (!filter_value(key, is_str, num, RB_CTRL_TIMEOUT_MIN_MS, RB_CTRL_TIMEOUT_MAX_MS, &out->tier2_shutdown_ms, err, err_len)) {
                    return false;
                }
            } else if (strcmp(key, "cookware") == 0) {
                bit = SEEN_COOKWARE;
                if (!is_str || (strcmp(str, "mixed") != 0 && strcmp(str, "light") != 0 && strcmp(str, "heavy") != 0 &&
                                strcmp(str, "liquid") != 0)) {
                    fail(err, err_len, "cookware must be mixed, light, heavy or liquid");
                    return false;
                }
                snprintf(out->cookware, sizeof(out->cookware), "%s", str);
            } else {
                fail(err, err_len, "unknown key '%s'", key);
                return false;
            }
            if (seen & bit) {
                fail(err, err_len, "duplicate key '%s'", key);
                return false;
            }
            seen |= bit;
        } while (eat(&s, ','));
        if (!eat(&s, '}')) {
            fail(err, err_len, "malformed JSON");
            return false;
        }
    }
    skip_ws(&s);
    if (s.p != s.end) {
        fail(err, err_len, "trailing data after the object");
        return false;
    }
    if ((seen & SEEN_ALL) != SEEN_ALL) {
        fail(err, err_len, "schema_version, type, controller_id, request_id and action are all required");
        return false;
    }
    const unsigned filter = seen & SEEN_FILTER;
    if (out->action == RB_CTRL_ACTION_PRESENCE_FILTER ? filter != SEEN_FILTER : filter != 0) {
        fail(err, err_len, "presence_filter needs all five values, other actions none");
        return false;
    }
    if ((out->action == RB_CTRL_ACTION_COOKING_TEMP) != ((seen & SEEN_COOKING) != 0)) {
        fail(err, err_len, "cooking_temp_dc goes with cooking_temp only, and it needs it");
        return false;
    }
    if ((out->action == RB_CTRL_ACTION_COOKWARE) != ((seen & SEEN_COOKWARE) != 0)) {
        fail(err, err_len, "cookware goes with the cookware action only, and it needs it");
        return false;
    }
    if (out->action == RB_CTRL_ACTION_TIERS ? (seen & SEEN_TIERS) != SEEN_TIERS : (seen & SEEN_TIERS) != 0) {
        fail(err, err_len, "tiers needs all eight tier values, other actions none");
        return false;
    }
    if (out->action == RB_CTRL_ACTION_RADAR_RULES ? (seen & SEEN_RADAR) != SEEN_RADAR : (seen & SEEN_RADAR) != 0) {
        fail(err, err_len, "radar_rules needs radar_mode, radar_quiet_gap_ms, radar_dwell_ms, radar_stationary_min_ms, "
                           "radar_settle_window_ms, radar_settle_span_cm and radar_zone_max_cm, other actions none");
        return false;
    }
    if (out->action == RB_CTRL_ACTION_ATTENDANCE ? (seen & SEEN_ATTENDANCE) != SEEN_ATTENDANCE
                                                  : (seen & SEEN_ATTENDANCE) != 0) {
        fail(err, err_len, "attendance needs pause_budget_ms, pause_budget_pct and presence_fusion, other actions none");
        return false;
    }
    return true;
}
