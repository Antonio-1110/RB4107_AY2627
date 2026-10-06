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

enum { SEEN_VERSION = 1, SEEN_TYPE = 2, SEEN_ID = 4, SEEN_REQUEST = 8, SEEN_ACTION = 16, SEEN_ALL = 31 };

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
                if (!is_str || strcmp(str, "reset") != 0) {
                    fail(err, err_len, "action must be \"reset\"");
                    return false;
                }
                out->action = RB_CTRL_ACTION_RESET;
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
    if (seen != SEEN_ALL) {
        fail(err, err_len, "schema_version, type, controller_id, request_id and action are all required");
        return false;
    }
    return true;
}
