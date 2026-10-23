#include "rb_log_capture.h"

#include <string.h>

/* Copy text without colour codes (ESC [ ... m) and line ends; anything not printable ASCII becomes '?'. */
static size_t clean(const char *text, size_t len, char *out, size_t cap)
{
    size_t n = 0;
    for (size_t i = 0; i < len && n + 1 < cap; i++) {
        const unsigned char c = (unsigned char)text[i];
        if (c == 0x1b) {
            while (i < len && text[i] != 'm') {
                i++;
            }
            continue;
        }
        if (c == '\r' || c == '\n') {
            continue;
        }
        out[n++] = (c >= 0x20 && c < 0x7f) ? (char)c : (c == '\t' ? ' ' : '?');
    }
    while (n > 0 && out[n - 1] == ' ') {
        n--;
    }
    out[n] = '\0';
    return n;
}

bool rb_log_line_parse(const char *text, size_t len, uint32_t uptime_ms, rb_log_line_t *out)
{
    char line[RB_LOG_LINE_MSG_LEN + RB_LOG_LINE_TAG_LEN + 48];
    const size_t n = clean(text, len, line, sizeof(line));
    if (n == 0) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->uptime_ms = uptime_ms;
    out->level = 'I';
    strcpy(out->tag, "-");
    const char *msg = line;

    /* "W (1234) SAFETY: text"; Wi-Fi's own lines have no space after the colon ("I (99) wifi:mode : sta"). */
    const char *close = n > 4 && strchr("EWIDV", line[0]) != NULL && line[1] == ' ' && line[2] == '(' ? strchr(line, ')') : NULL;
    if (close != NULL && close[1] == ' ') {
        const char *tag = close + 2;
        const char *colon = strchr(tag, ':');
        const size_t tag_len = colon != NULL ? (size_t)(colon - tag) : 0;
        if (tag_len > 0 && tag_len < RB_LOG_LINE_TAG_LEN && memchr(tag, ' ', tag_len) == NULL) {
            out->level = line[0];
            memcpy(out->tag, tag, tag_len);
            out->tag[tag_len] = '\0';
            msg = colon + 1;
            if (*msg == ' ') {
                msg++;
            }
        }
    }
    strlcpy(out->msg, msg, sizeof(out->msg));
    return true;
}
