#include "rb_json_writer.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void put_raw(rb_json_writer_t *w, const char *s, size_t n)
{
    if (w->overflow || w->len + n + 1 > w->cap) {
        w->overflow = true;
        return;
    }
    memcpy(w->buf + w->len, s, n);
    w->len += n;
    w->buf[w->len] = '\0';
}

static void put(rb_json_writer_t *w, const char *s)
{
    put_raw(w, s, strlen(s));
}

/* Comma handling before any value or key. */
static void separator(rb_json_writer_t *w)
{
    if (w->after_key) {
        w->after_key = false;
        return;
    }
    if (w->depth > 0) {
        if (!w->first[w->depth - 1]) {
            put(w, ",");
        }
        w->first[w->depth - 1] = false;
    }
}

static void json_open(rb_json_writer_t *w, const char *bracket)
{
    separator(w);
    put(w, bracket);
    if (w->depth >= RB_JSON_MAX_DEPTH) {
        w->overflow = true;
        return;
    }
    w->first[w->depth++] = true;
}

static void json_close(rb_json_writer_t *w, const char *bracket)
{
    if (w->depth == 0) {
        w->overflow = true;
        return;
    }
    w->depth--;
    put(w, bracket);
}

static void put_escaped(rb_json_writer_t *w, const char *s)
{
    put(w, "\"");
    for (; *s != '\0'; s++) {
        const unsigned char c = (unsigned char)*s;
        char esc[8];
        switch (c) {
        case '"': put(w, "\\\""); break;
        case '\\': put(w, "\\\\"); break;
        case '\n': put(w, "\\n"); break;
        case '\r': put(w, "\\r"); break;
        case '\t': put(w, "\\t"); break;
        default:
            if (c < 0x20) {
                snprintf(esc, sizeof(esc), "\\u%04x", c);
                put(w, esc);
            } else {
                put_raw(w, (const char *)&c, 1);
            }
        }
    }
    put(w, "\"");
}

void rb_json_init(rb_json_writer_t *w, char *buf, size_t cap)
{
    memset(w, 0, sizeof(*w));
    w->buf = buf;
    w->cap = cap;
    if (cap > 0) {
        buf[0] = '\0';
    } else {
        w->overflow = true;
    }
}

void rb_json_obj_begin(rb_json_writer_t *w) { json_open(w, "{"); }
void rb_json_obj_end(rb_json_writer_t *w) { json_close(w, "}"); }
void rb_json_arr_begin(rb_json_writer_t *w) { json_open(w, "["); }
void rb_json_arr_end(rb_json_writer_t *w) { json_close(w, "]"); }

void rb_json_key(rb_json_writer_t *w, const char *key)
{
    separator(w);
    put_escaped(w, key);
    put(w, ":");
    w->after_key = true;
}

void rb_json_str(rb_json_writer_t *w, const char *value)
{
    if (value == NULL) {
        rb_json_null(w);
        return;
    }
    separator(w);
    put_escaped(w, value);
}

void rb_json_int(rb_json_writer_t *w, int64_t value)
{
    char tmp[24];
    snprintf(tmp, sizeof(tmp), "%lld", (long long)value);
    separator(w);
    put(w, tmp);
}

void rb_json_num(rb_json_writer_t *w, float value, int decimals)
{
    if (!isfinite(value)) {
        rb_json_null(w);
        return;
    }
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%.*f", decimals, (double)value);
    separator(w);
    put(w, tmp);
}

void rb_json_bool(rb_json_writer_t *w, bool value)
{
    separator(w);
    put(w, value ? "true" : "false");
}

void rb_json_null(rb_json_writer_t *w)
{
    separator(w);
    put(w, "null");
}

void rb_json_base64(rb_json_writer_t *w, const uint8_t *data, size_t len)
{
    static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    separator(w);
    put(w, "\"");
    for (size_t i = 0; i < len; i += 3) {
        const uint32_t b0 = data[i];
        const uint32_t b1 = i + 1 < len ? data[i + 1] : 0;
        const uint32_t b2 = i + 2 < len ? data[i + 2] : 0;
        const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        const char quad[4] = {
            ALPHABET[(triple >> 18) & 0x3F],
            ALPHABET[(triple >> 12) & 0x3F],
            i + 1 < len ? ALPHABET[(triple >> 6) & 0x3F] : '=',
            i + 2 < len ? ALPHABET[triple & 0x3F] : '=',
        };
        put_raw(w, quad, sizeof(quad));
    }
    put(w, "\"");
}

size_t rb_json_finish(rb_json_writer_t *w)
{
    return (w->overflow || w->depth != 0 || w->after_key) ? 0 : w->len;
}
