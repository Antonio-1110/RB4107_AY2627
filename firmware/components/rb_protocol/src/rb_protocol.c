#include "rb_protocol.h"

#include <math.h>
#include <string.h>

#define DIST_UNKNOWN 0xFFFFu
#define CENTI_UNKNOWN INT16_MIN

enum {
    FLAG_PRESENCE_VALID = 1u << 0,
    FLAG_PRESENCE_DETECTED = 1u << 1,
    FLAG_PRESENCE_MOVING = 1u << 2,
    FLAG_PRESENCE_STATIONARY = 1u << 3,
    FLAG_THERMAL_VALID = 1u << 4,
};

/* ---- little-endian writer / reader ---- */

typedef struct {
    uint8_t *p;
    size_t n;
} writer_t;

static void put_u8(writer_t *w, uint8_t v)
{
    w->p[w->n++] = v;
}

static void put_u16(writer_t *w, uint16_t v)
{
    put_u8(w, (uint8_t)(v & 0xFF));
    put_u8(w, (uint8_t)(v >> 8));
}

static void put_u32(writer_t *w, uint32_t v)
{
    put_u16(w, (uint16_t)(v & 0xFFFF));
    put_u16(w, (uint16_t)(v >> 16));
}

typedef struct {
    const uint8_t *p;
    size_t n;
} reader_t;

static uint8_t get_u8(reader_t *r)
{
    return r->p[r->n++];
}

static uint16_t get_u16(reader_t *r)
{
    uint16_t lo = get_u8(r);
    return (uint16_t)(lo | (get_u8(r) << 8));
}

static uint32_t get_u32(reader_t *r)
{
    uint32_t lo = get_u16(r);
    return lo | ((uint32_t)get_u16(r) << 16);
}

/* ---- fixed-point helpers ---- */

static int16_t to_centi(float v)
{
    if (!isfinite(v)) {
        return CENTI_UNKNOWN;
    }
    float c = roundf(v * 100.0f);
    if (c > INT16_MAX) {
        return INT16_MAX;
    }
    if (c <= INT16_MIN) {
        return INT16_MIN + 1; /* INT16_MIN is reserved for "unknown" */
    }
    return (int16_t)c;
}

static float from_centi(int16_t v)
{
    return v == CENTI_UNKNOWN ? NAN : (float)v / 100.0f;
}

static uint16_t to_cm(float metres)
{
    if (!isfinite(metres) || metres < 0.0f) {
        return DIST_UNKNOWN;
    }
    float cm = roundf(metres * 100.0f);
    return cm >= (float)DIST_UNKNOWN ? (uint16_t)(DIST_UNKNOWN - 1) : (uint16_t)cm;
}

static float from_cm(uint16_t cm)
{
    return cm == DIST_UNKNOWN ? NAN : (float)cm / 100.0f;
}

static size_t packet_len(rb_msg_type_t type)
{
    switch (type) {
    case RB_MSG_SENSOR_DATA: return RB_PKT_SENSOR_DATA_LEN;
    case RB_MSG_HEARTBEAT: return RB_PKT_HEARTBEAT_LEN;
    case RB_MSG_SENSOR_FAULT: return RB_PKT_SENSOR_FAULT_LEN;
    default: return 0;
    }
}

uint16_t rb_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

size_t rb_protocol_encode(const rb_packet_t *pkt, uint8_t *buf, size_t buf_len)
{
    const size_t total = packet_len(pkt->type);
    if (total == 0 || buf == NULL || buf_len < total) {
        return 0;
    }
    writer_t w = {.p = buf, .n = 0};
    put_u8(&w, RB_PROTOCOL_MAGIC & 0xFF);
    put_u8(&w, RB_PROTOCOL_MAGIC >> 8);
    put_u8(&w, RB_PROTOCOL_VERSION);
    put_u8(&w, (uint8_t)pkt->type);
    put_u32(&w, pkt->node_id);
    put_u32(&w, pkt->sequence);
    put_u32(&w, pkt->uptime_ms);

    switch (pkt->type) {
    case RB_MSG_SENSOR_DATA: {
        const presence_reading_t *p = &pkt->body.sensor.presence;
        const thermal_reading_t *t = &pkt->body.sensor.thermal;
        uint8_t flags = 0;
        flags |= p->valid ? FLAG_PRESENCE_VALID : 0;
        flags |= p->presence_detected ? FLAG_PRESENCE_DETECTED : 0;
        flags |= p->moving_target ? FLAG_PRESENCE_MOVING : 0;
        flags |= p->stationary_target ? FLAG_PRESENCE_STATIONARY : 0;
        flags |= t->valid ? FLAG_THERMAL_VALID : 0;
        put_u8(&w, flags);
        put_u16(&w, to_cm(p->distance_m));
        put_u32(&w, p->timestamp_ms);
        put_u16(&w, (uint16_t)to_centi(t->max_temp_c));
        put_u16(&w, (uint16_t)to_centi(t->min_temp_c));
        put_u16(&w, (uint16_t)to_centi(t->mean_temp_c));
        put_u16(&w, (uint16_t)to_centi(t->hot_region_temp_c));
        put_u16(&w, (uint16_t)to_centi(t->temp_rate_c_per_min));
        put_u16(&w, t->pixels_above_threshold);
        put_u32(&w, t->timestamp_ms);
        break;
    }
    case RB_MSG_HEARTBEAT:
        put_u16(&w, pkt->body.heartbeat.fault_flags);
        put_u32(&w, pkt->body.heartbeat.tx_ok);
        put_u32(&w, pkt->body.heartbeat.tx_fail);
        break;
    case RB_MSG_SENSOR_FAULT:
        put_u16(&w, pkt->body.fault.fault_flags);
        put_u16(&w, pkt->body.fault.changed_flags);
        put_u32(&w, (uint32_t)pkt->body.fault.detail);
        break;
    }
    if (w.n + RB_CRC_LEN != total) {
        return 0; /* body layout and the RB_BODY_*_LEN constants disagree */
    }
    put_u16(&w, rb_crc16(buf, w.n));
    return w.n;
}

rb_decode_result_t rb_protocol_decode(const uint8_t *buf, size_t len, rb_packet_t *out)
{
    if (buf == NULL || len < RB_HEADER_LEN + RB_CRC_LEN) {
        return RB_DECODE_ERR_LENGTH;
    }
    if ((buf[0] | (buf[1] << 8)) != RB_PROTOCOL_MAGIC) {
        return RB_DECODE_ERR_MAGIC;
    }
    if (buf[2] != RB_PROTOCOL_VERSION) {
        return RB_DECODE_ERR_VERSION;
    }
    const rb_msg_type_t type = (rb_msg_type_t)buf[3];
    const size_t expected = packet_len(type);
    if (expected == 0) {
        return RB_DECODE_ERR_TYPE;
    }
    if (len != expected) {
        return RB_DECODE_ERR_LENGTH;
    }
    if (rb_crc16(buf, len - RB_CRC_LEN) != (uint16_t)(buf[len - 2] | (buf[len - 1] << 8))) {
        return RB_DECODE_ERR_CRC;
    }

    memset(out, 0, sizeof(*out));
    reader_t r = {.p = buf, .n = 4};
    out->protocol_version = buf[2];
    out->type = type;
    out->node_id = get_u32(&r);
    out->sequence = get_u32(&r);
    out->uptime_ms = get_u32(&r);

    switch (type) {
    case RB_MSG_SENSOR_DATA: {
        presence_reading_t *p = &out->body.sensor.presence;
        thermal_reading_t *t = &out->body.sensor.thermal;
        const uint8_t flags = get_u8(&r);
        p->valid = flags & FLAG_PRESENCE_VALID;
        p->presence_detected = flags & FLAG_PRESENCE_DETECTED;
        p->moving_target = flags & FLAG_PRESENCE_MOVING;
        p->stationary_target = flags & FLAG_PRESENCE_STATIONARY;
        p->distance_m = from_cm(get_u16(&r));
        p->timestamp_ms = get_u32(&r);
        t->valid = flags & FLAG_THERMAL_VALID;
        t->max_temp_c = from_centi((int16_t)get_u16(&r));
        t->min_temp_c = from_centi((int16_t)get_u16(&r));
        t->mean_temp_c = from_centi((int16_t)get_u16(&r));
        t->hot_region_temp_c = from_centi((int16_t)get_u16(&r));
        t->temp_rate_c_per_min = from_centi((int16_t)get_u16(&r));
        t->pixels_above_threshold = get_u16(&r);
        t->timestamp_ms = get_u32(&r);
        break;
    }
    case RB_MSG_HEARTBEAT:
        out->body.heartbeat.fault_flags = get_u16(&r);
        out->body.heartbeat.tx_ok = get_u32(&r);
        out->body.heartbeat.tx_fail = get_u32(&r);
        break;
    case RB_MSG_SENSOR_FAULT:
        out->body.fault.fault_flags = get_u16(&r);
        out->body.fault.changed_flags = get_u16(&r);
        out->body.fault.detail = (int32_t)get_u32(&r);
        break;
    }
    return RB_DECODE_OK;
}

const char *rb_decode_result_name(rb_decode_result_t result)
{
    switch (result) {
    case RB_DECODE_OK: return "ok";
    case RB_DECODE_ERR_LENGTH: return "bad length";
    case RB_DECODE_ERR_MAGIC: return "bad magic";
    case RB_DECODE_ERR_VERSION: return "unsupported version";
    case RB_DECODE_ERR_TYPE: return "unknown message type";
    case RB_DECODE_ERR_CRC: return "CRC mismatch";
    default: return "?";
    }
}

const char *rb_msg_type_name(rb_msg_type_t type)
{
    switch (type) {
    case RB_MSG_SENSOR_DATA: return "SENSOR_DATA";
    case RB_MSG_HEARTBEAT: return "HEARTBEAT";
    case RB_MSG_SENSOR_FAULT: return "SENSOR_FAULT";
    default: return "?";
    }
}
