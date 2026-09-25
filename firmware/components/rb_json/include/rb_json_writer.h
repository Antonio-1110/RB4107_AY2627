#pragma once

/*
 * Minimal streaming JSON writer into a caller-supplied buffer: no heap, no
 * recursion. It inserts commas, escapes strings, writes non-finite numbers
 * as null, and flags overflow instead of writing past the end.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RB_JSON_MAX_DEPTH 8

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    bool overflow;
    uint8_t depth;
    bool first[RB_JSON_MAX_DEPTH];
    bool after_key;
} rb_json_writer_t;

void rb_json_init(rb_json_writer_t *w, char *buf, size_t cap);
void rb_json_obj_begin(rb_json_writer_t *w);
void rb_json_obj_end(rb_json_writer_t *w);
void rb_json_arr_begin(rb_json_writer_t *w);
void rb_json_arr_end(rb_json_writer_t *w);
void rb_json_key(rb_json_writer_t *w, const char *key);
void rb_json_str(rb_json_writer_t *w, const char *value);   /* NULL -> null */
void rb_json_int(rb_json_writer_t *w, int64_t value);
void rb_json_num(rb_json_writer_t *w, float value, int decimals); /* NaN/Inf -> null */
void rb_json_bool(rb_json_writer_t *w, bool value);
void rb_json_null(rb_json_writer_t *w);

/* Length of the finished document, or 0 on overflow / unbalanced nesting. */
size_t rb_json_finish(rb_json_writer_t *w);

#ifdef __cplusplus
}
#endif
