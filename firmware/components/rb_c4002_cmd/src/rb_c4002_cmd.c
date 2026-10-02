#include "rb_c4002_cmd.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rb_json_writer.h"

/* ---- a minimal parser for one flat JSON object ---- */

#define KEY_LEN 32
#define STR_LEN 40
#define ARR_MAX RB_C4002_MAX_GATES

typedef enum { V_INT, V_STR, V_ARR, V_NULL } vtype_t;

typedef struct {
    char key[KEY_LEN];
    vtype_t type;
    long num;                     /* V_INT (true/false become 1/0) */
    char str[STR_LEN];            /* V_STR */
    long arr[ARR_MAX];            /* V_ARR */
    size_t n;
} kv_t;

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

/* Strings are names and short words: escapes are not needed and rejected. */
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

static bool read_word(scan_t *s, const char *word)
{
    const size_t n = strlen(word);
    if ((size_t)(s->end - s->p) >= n && memcmp(s->p, word, n) == 0) {
        s->p += n;
        return true;
    }
    return false;
}

/* Integers only (a fraction or exponent is an error). */
static bool read_int(scan_t *s, long *out)
{
    skip_ws(s);
    if (read_word(s, "true")) {
        *out = 1;
        return true;
    }
    if (read_word(s, "false")) {
        *out = 0;
        return true;
    }
    const char *start = s->p;
    if (s->p < s->end && *s->p == '-') {
        s->p++;
    }
    long v = 0;
    while (s->p < s->end && *s->p >= '0' && *s->p <= '9') {
        if (v > 1000000) {
            return false;
        }
        v = v * 10 + (*s->p++ - '0');
    }
    if (s->p == start || (s->p - start == 1 && *start == '-')) {
        return false;
    }
    if (s->p < s->end && (*s->p == '.' || *s->p == 'e' || *s->p == 'E')) {
        return false;
    }
    *out = *start == '-' ? -v : v;
    return true;
}

static bool read_value(scan_t *s, kv_t *kv)
{
    skip_ws(s);
    if (s->p >= s->end) {
        return false;
    }
    if (*s->p == '"') {
        kv->type = V_STR;
        return read_string(s, kv->str, sizeof(kv->str));
    }
    if (read_word(s, "null")) {
        kv->type = V_NULL;
        return true;
    }
    if (*s->p == '[') {
        s->p++;
        kv->type = V_ARR;
        kv->n = 0;
        if (eat(s, ']')) {
            return true;
        }
        do {
            if (kv->n >= ARR_MAX || !read_int(s, &kv->arr[kv->n++])) {
                return false;
            }
        } while (eat(s, ','));
        return eat(s, ']');
    }
    kv->type = V_INT;
    return read_int(s, &kv->num);
}

/* ---- command fields ---- */

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

static bool want_int(const kv_t *kv, long min, long max, long *out, char *err, size_t err_len)
{
    if (kv->type != V_INT || kv->num < min || kv->num > max) {
        fail(err, err_len, "%s must be an integer %ld..%ld", kv->key, min, max);
        return false;
    }
    *out = kv->num;
    return true;
}

static bool want_array(const kv_t *kv, long max, uint8_t *out, char *err, size_t err_len)
{
    if (kv->type != V_ARR || kv->n != RB_C4002_MAX_GATES) {
        fail(err, err_len, "%s must be an array of 25 values (one per gate)", kv->key);
        return false;
    }
    for (size_t i = 0; i < kv->n; i++) {
        if (kv->arr[i] < 0 || kv->arr[i] > max) {
            fail(err, err_len, "%s[%u] must be 0..%ld", kv->key, (unsigned)i, max);
            return false;
        }
        out[i] = (uint8_t)kv->arr[i];
    }
    return true;
}

static uint32_t to_mask(const uint8_t *gates)
{
    uint32_t mask = 0;
    for (unsigned i = 0; i < RB_C4002_MAX_GATES; i++) {
        mask |= gates[i] ? (1u << i) : 0;
    }
    return mask;
}

const char *rb_c4002_sensitivity_name(uint8_t sensitivity)
{
    static const char *const names[] = {"low", "mid", "high", "custom"};
    return sensitivity < 4 ? names[sensitivity] : "?";
}

static bool want_sensitivity(const kv_t *kv, uint8_t *out, char *err, size_t err_len)
{
    for (uint8_t i = 0; kv->type == V_STR && i < 4; i++) {
        if (strcmp(kv->str, rb_c4002_sensitivity_name(i)) == 0) {
            *out = i;
            return true;
        }
    }
    fail(err, err_len, "%s must be \"low\", \"mid\", \"high\" or \"custom\"", kv->key);
    return false;
}

static bool parse_action(const char *name, uint8_t *out)
{
    for (uint8_t a = RB_C4002_ACTION_APPLY; a <= RB_C4002_ACTION_RESET; a++) {
        if (strcmp(name, rb_c4002_action_name(a)) == 0) {
            *out = a;
            return true;
        }
    }
    return false;
}

/* One key of the object. Settings keys set their RB_C4002_F_* bit. */
static bool apply_kv(const kv_t *kv, rb_c4002_cmd_t *cmd, bool *seen_type, char *err, size_t err_len)
{
    rb_c4002_config_msg_t *m = &cmd->msg;
    rb_c4002_params_t *p = &m->params;
    long v;
    uint8_t gates[RB_C4002_MAX_GATES];

    if (strcmp(kv->key, "schema_version") == 0) {
        return want_int(kv, 1, 1000, &v, err, err_len);
    }
    if (strcmp(kv->key, "type") == 0) {
        *seen_type = kv->type == V_STR && strcmp(kv->str, "c4002_command") == 0;
        if (!*seen_type) {
            fail(err, err_len, "type must be \"c4002_command\"");
        }
        return *seen_type;
    }
    if (strcmp(kv->key, "controller_id") == 0) {
        if (kv->type != V_STR) {
            fail(err, err_len, "controller_id must be a string");
            return false;
        }
        snprintf(cmd->controller_id, sizeof(cmd->controller_id), "%s", kv->str);
        return true;
    }
    if (strcmp(kv->key, "request_id") == 0) {
        if (!want_int(kv, 0, 65535, &v, err, err_len)) {
            return false;
        }
        m->request_id = (uint16_t)v;
        return true;
    }
    if (strcmp(kv->key, "action") == 0) {
        if (kv->type != V_STR || !parse_action(kv->str, &m->action)) {
            fail(err, err_len, "action must be \"apply\", \"calibrate\", \"read\" or \"reset\"");
            return false;
        }
        return true;
    }
    if (strcmp(kv->key, "calibration_delay_s") == 0) {
        if (!want_int(kv, 0, 600, &v, err, err_len)) {
            return false;
        }
        m->calib_delay_s = (uint16_t)v;
        return true;
    }
    if (strcmp(kv->key, "calibration_duration_s") == 0) {
        if (!want_int(kv, 1, 600, &v, err, err_len)) {
            return false;
        }
        m->calib_duration_s = (uint16_t)v;
        return true;
    }

    /* Settings (action "apply"). */
    uint16_t bit = 0;
    bool ok = true;
    if (strcmp(kv->key, "report_period_ds") == 0) {
        ok = want_int(kv, 1, 255, &v, err, err_len);
        p->report_period_ds = (uint8_t)v;
        bit = RB_C4002_F_REPORT_PERIOD;
    } else if (strcmp(kv->key, "range_min_cm") == 0) {
        ok = want_int(kv, 0, 1100, &v, err, err_len);
        p->range_min_cm = (uint16_t)v;
        bit = RB_C4002_F_RANGE_MIN;
    } else if (strcmp(kv->key, "range_max_cm") == 0) {
        ok = want_int(kv, 0, 1100, &v, err, err_len);
        p->range_max_cm = (uint16_t)v;
        bit = RB_C4002_F_RANGE_MAX;
    } else if (strcmp(kv->key, "resolution_cm") == 0) {
        ok = kv->type == V_INT && (kv->num == 20 || kv->num == 80);
        if (!ok) {
            fail(err, err_len, "resolution_cm must be 20 or 80");
        }
        p->resolution = kv->num == 20 ? RB_C4002_RES_20CM : RB_C4002_RES_80CM;
        bit = RB_C4002_F_RESOLUTION;
    } else if (strcmp(kv->key, "motion_sensitivity") == 0) {
        ok = want_sensitivity(kv, &p->motion_sensitivity, err, err_len);
        bit = RB_C4002_F_MOTION_SENS;
    } else if (strcmp(kv->key, "presence_sensitivity") == 0) {
        ok = want_sensitivity(kv, &p->presence_sensitivity, err, err_len);
        bit = RB_C4002_F_PRESENCE_SENS;
    } else if (strcmp(kv->key, "disappear_delay_s") == 0) {
        ok = want_int(kv, 0, 65535, &v, err, err_len);
        p->disappear_delay_s = (uint16_t)v;
        bit = RB_C4002_F_DISAPPEAR_DELAY;
    } else if (strcmp(kv->key, "lock_time_ds") == 0) {
        ok = want_int(kv, 2, 100, &v, err, err_len);
        p->lock_time_ds = (uint8_t)v;
        bit = RB_C4002_F_LOCK_TIME;
    } else if (strcmp(kv->key, "motion_gates") == 0) {
        ok = want_array(kv, 1, gates, err, err_len);
        p->motion_gate_mask = to_mask(gates);
        bit = RB_C4002_F_MOTION_GATES;
    } else if (strcmp(kv->key, "presence_gates") == 0) {
        ok = want_array(kv, 1, gates, err, err_len);
        p->presence_gate_mask = to_mask(gates);
        bit = RB_C4002_F_PRESENCE_GATES;
    } else if (strcmp(kv->key, "motion_thresholds") == 0) {
        ok = want_array(kv, 99, p->motion_thresholds, err, err_len);
        bit = RB_C4002_F_MOTION_THRESH;
    } else if (strcmp(kv->key, "presence_thresholds") == 0) {
        ok = want_array(kv, 99, p->presence_thresholds, err, err_len);
        bit = RB_C4002_F_PRESENCE_THRESH;
    } else {
        fail(err, err_len, "unknown key '%s'", kv->key);
        return false;
    }
    m->field_mask |= bit;
    return ok;
}

bool rb_c4002_cmd_parse(const char *json, size_t len, rb_c4002_cmd_t *out, char *err, size_t err_len)
{
    memset(out, 0, sizeof(*out));
    out->msg.calib_delay_s = RB_C4002_CMD_DEFAULT_CALIB_DELAY_S;
    out->msg.calib_duration_s = RB_C4002_CMD_DEFAULT_CALIB_DURATION_S;
    fail(err, err_len, "malformed JSON");
    if (json == NULL) {
        return false;
    }

    scan_t s = {.p = json, .end = json + len};
    bool seen_type = false;
    static kv_t kv; /* ~200 B: kept off the (small) MQTT task stack; only the MQTT task parses */
    if (!eat(&s, '{')) {
        return false;
    }
    if (!eat(&s, '}')) {
        do {
            memset(&kv, 0, sizeof(kv));
            if (!read_string(&s, kv.key, sizeof(kv.key)) || !eat(&s, ':') || !read_value(&s, &kv)) {
                fail(err, err_len, "malformed JSON near char %u", (unsigned)(s.p - json));
                return false;
            }
            if (!apply_kv(&kv, out, &seen_type, err, err_len)) {
                return false;
            }
        } while (eat(&s, ','));
        if (!eat(&s, '}')) {
            fail(err, err_len, "malformed JSON near char %u", (unsigned)(s.p - json));
            return false;
        }
    }
    skip_ws(&s);
    if (s.p != s.end && !(s.p + 1 == s.end && *s.p == '\0')) {
        fail(err, err_len, "trailing data after the JSON object");
        return false;
    }

    const rb_c4002_config_msg_t *m = &out->msg;
    if (!seen_type) {
        fail(err, err_len, "type must be \"c4002_command\"");
        return false;
    }
    if (m->action == 0) {
        fail(err, err_len, "action is required");
        return false;
    }
    if (m->action == RB_C4002_ACTION_APPLY && m->field_mask == 0) {
        fail(err, err_len, "apply needs at least one setting");
        return false;
    }
    if (m->action != RB_C4002_ACTION_APPLY && m->field_mask != 0) {
        fail(err, err_len, "settings are only allowed with action \"apply\"");
        return false;
    }
    if ((m->field_mask & RB_C4002_F_RANGE_MIN) && (m->field_mask & RB_C4002_F_RANGE_MAX) &&
        m->params.range_min_cm > m->params.range_max_cm) {
        fail(err, err_len, "range_min_cm > range_max_cm");
        return false;
    }
    if (err != NULL && err_len > 0) {
        err[0] = '\0';
    }
    return true;
}

bool rb_c4002_cmd_topic_node(const char *topic, const char *prefix, uint32_t *node_id)
{
    const size_t plen = strlen(prefix);
    static const char head[] = "/sensors/node_";
    static const char tail[] = "/c4002_set";
    if (strncmp(topic, prefix, plen) != 0 || strncmp(topic + plen, head, sizeof(head) - 1) != 0) {
        return false;
    }
    const char *digits = topic + plen + sizeof(head) - 1;
    char *end;
    const unsigned long id = strtoul(digits, &end, 10);
    if (end == digits || strcmp(end, tail) != 0 || id == 0 || id > 255) {
        return false;
    }
    *node_id = (uint32_t)id;
    return true;
}

/* ---- reply JSON ---- */

static const char *reply_result(const rb_c4002_reply_t *r)
{
    switch (r->kind) {
    case RB_C4002_REPLY_NODE: return rb_c4002_result_name(r->ack.result);
    case RB_C4002_REPLY_REJECTED: return "rejected";
    case RB_C4002_REPLY_UNKNOWN_NODE: return "unknown_node";
    case RB_C4002_REPLY_SEND_FAILED: return "send_failed";
    case RB_C4002_REPLY_NO_REPLY: return "no_reply";
    default: return "?";
    }
}

static void int_array(rb_json_writer_t *w, const char *key, const uint8_t *v)
{
    rb_json_key(w, key);
    rb_json_arr_begin(w);
    for (unsigned i = 0; i < RB_C4002_MAX_GATES; i++) {
        rb_json_int(w, v[i]);
    }
    rb_json_arr_end(w);
}

static void gate_array(rb_json_writer_t *w, const char *key, uint32_t mask)
{
    uint8_t gates[RB_C4002_MAX_GATES];
    for (unsigned i = 0; i < RB_C4002_MAX_GATES; i++) {
        gates[i] = (mask >> i) & 1u;
    }
    int_array(w, key, gates);
}

static void settings_obj(rb_json_writer_t *w, const rb_c4002_params_t *p)
{
    rb_json_key(w, "settings");
    rb_json_obj_begin(w);
    rb_json_key(w, "report_period_ds");
    rb_json_int(w, p->report_period_ds);
    rb_json_key(w, "range_min_cm");
    rb_json_int(w, p->range_min_cm);
    rb_json_key(w, "range_max_cm");
    rb_json_int(w, p->range_max_cm);
    rb_json_key(w, "resolution_cm");
    rb_json_int(w, p->resolution == RB_C4002_RES_20CM ? 20 : 80);
    rb_json_key(w, "gate_count");
    rb_json_int(w, rb_c4002_gate_count(p->resolution));
    rb_json_key(w, "motion_sensitivity");
    rb_json_str(w, rb_c4002_sensitivity_name(p->motion_sensitivity));
    rb_json_key(w, "presence_sensitivity");
    rb_json_str(w, rb_c4002_sensitivity_name(p->presence_sensitivity));
    rb_json_key(w, "disappear_delay_s");
    rb_json_int(w, p->disappear_delay_s);
    rb_json_key(w, "lock_time_ds");
    rb_json_int(w, p->lock_time_ds);
    gate_array(w, "motion_gates", p->motion_gate_mask);
    gate_array(w, "presence_gates", p->presence_gate_mask);
    if (p->thresholds_known & RB_C4002_THRESH_MOTION) {
        int_array(w, "motion_thresholds", p->motion_thresholds);
    } else {
        rb_json_key(w, "motion_thresholds");
        rb_json_null(w);
    }
    if (p->thresholds_known & RB_C4002_THRESH_PRESENCE) {
        int_array(w, "presence_thresholds", p->presence_thresholds);
    } else {
        rb_json_key(w, "presence_thresholds");
        rb_json_null(w);
    }
    rb_json_obj_end(w);
}

size_t rb_c4002_reply_json(const rb_c4002_reply_t *r, char *buf, size_t len)
{
    rb_json_writer_t w;
    rb_json_init(&w, buf, len);
    rb_json_begin_message(&w, &r->hdr, "c4002_config");
    rb_json_key(&w, "sensor_node");
    rb_json_str(&w, r->sensor_node);
    rb_json_key(&w, "request_id");
    rb_json_int(&w, r->request_id);
    rb_json_key(&w, "action");
    rb_json_str(&w, r->action != 0 ? rb_c4002_action_name(r->action) : NULL);
    rb_json_key(&w, "result");
    rb_json_str(&w, reply_result(r));
    rb_json_key(&w, "error");
    rb_json_str(&w, r->error);
    rb_json_key(&w, "calibration_remaining_s");
    rb_json_int(&w, r->has_ack ? r->ack.calib_remaining_s : 0);
    rb_json_key(&w, "saved");
    r->has_ack ? rb_json_bool(&w, r->ack.saved) : rb_json_null(&w);
    if (r->has_ack) {
        settings_obj(&w, &r->ack.params);
    } else {
        rb_json_key(&w, "settings");
        rb_json_null(&w);
    }
    rb_json_obj_end(&w);
    return rb_json_finish(&w);
}

static const char *live_target_name(uint8_t state)
{
    switch (state) {
    case 0: return "none";
    case 1: return "stationary";
    case 2: return "moving";
    default: return NULL;
    }
}

static const char *live_direction_name(uint8_t direction)
{
    switch (direction) {
    case 0: return "away";
    case 1: return "none";
    case 2: return "approaching";
    default: return NULL;
    }
}

size_t rb_c4002_live_json(const rb_c4002_live_msg_t *m, char *buf, size_t len)
{
    const rb_c4002_live_t *l = &m->live;
    rb_json_writer_t w;
    rb_json_init(&w, buf, len);
    rb_json_begin_message(&w, &m->hdr, "c4002_live");
    rb_json_key(&w, "sensor_node");
    rb_json_str(&w, m->sensor_node);
    rb_json_key(&w, "node_uptime_ms");
    rb_json_int(&w, m->node_uptime_ms);
    rb_json_key(&w, "results");
    rb_json_int(&w, l->results);
    rb_json_key(&w, "age_ms");
    rb_json_int(&w, l->age_ms);
    rb_json_key(&w, "target");
    rb_json_str(&w, live_target_name(l->target_state));
    rb_json_key(&w, "gate_size_cm");
    rb_json_int(&w, l->resolution == RB_C4002_RES_20CM ? 20 : 80);
    rb_json_key(&w, "presence_gates");
    rb_json_arr_begin(&w);
    for (unsigned i = 0; i < RB_C4002_MAX_GATES; i++) {
        if (l->presence_gate_mask & (1u << i)) {
            rb_json_int(&w, i);
        }
    }
    rb_json_arr_end(&w);
    rb_json_key(&w, "presence");
    rb_json_obj_begin(&w);
    rb_json_key(&w, "distance_cm");
    rb_json_int(&w, l->presence_distance_cm);
    rb_json_key(&w, "energy");
    rb_json_int(&w, l->presence_energy);
    rb_json_key(&w, "countdown_s");
    rb_json_int(&w, l->presence_countdown_s);
    rb_json_obj_end(&w);
    rb_json_key(&w, "motion");
    rb_json_obj_begin(&w);
    rb_json_key(&w, "distance_cm");
    rb_json_int(&w, l->motion_distance_cm);
    rb_json_key(&w, "speed_cm_s");
    rb_json_int(&w, l->motion_speed_cm_s);
    rb_json_key(&w, "energy");
    rb_json_int(&w, l->motion_energy);
    rb_json_key(&w, "direction");
    rb_json_str(&w, live_direction_name(l->motion_direction));
    rb_json_obj_end(&w);
    rb_json_key(&w, "light_lux");
    rb_json_num(&w, l->light_dlux / 10.0f, 1);
    rb_json_key(&w, "calibration_remaining_s");
    rb_json_int(&w, l->calib_remaining_s);
    rb_json_obj_end(&w);
    return rb_json_finish(&w);
}
