#include "c4002_proto.h"

#include <math.h>
#include <string.h>

static const uint8_t HEADER[4] = {0xFA, 0xF5, 0xAA, 0xA5};

/* Maximum range of the C4002 is 11 m; allow some margin before calling a distance bogus. */
#define C4002_MAX_PLAUSIBLE_CM 1200u

static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint16_t checksum(const uint8_t *p, size_t len)
{
    uint16_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum = (uint16_t)(sum + p[i]);
    }
    return sum;
}

void c4002_parser_reset(c4002_parser_t *parser)
{
    parser->len = 0;
    parser->expected = 0;
}

c4002_parse_status_t c4002_parser_feed(c4002_parser_t *parser, uint8_t byte, c4002_frame_t *out)
{
    if (parser->len < sizeof(HEADER)) {
        if (byte == HEADER[parser->len]) {
            parser->buf[parser->len++] = byte;
        } else {
            /* Restart, but let this byte begin a new header. */
            parser->len = 0;
            if (byte == HEADER[0]) {
                parser->buf[parser->len++] = byte;
            }
        }
        return C4002_PARSE_NONE;
    }

    parser->buf[parser->len++] = byte;

    if (parser->len == 6) {
        parser->expected = rd_u16(&parser->buf[4]);
        if (parser->expected < C4002_FRAME_MIN || parser->expected > sizeof(parser->buf)) {
            c4002_parser_reset(parser);
            return C4002_PARSE_ERR_LENGTH;
        }
    }
    if (parser->len < 6 || parser->len < parser->expected) {
        return C4002_PARSE_NONE;
    }

    /* Complete frame. */
    const uint16_t total = parser->expected;
    c4002_parser_reset(parser);
    if (checksum(parser->buf, total - 2u) != rd_u16(&parser->buf[total - 2u])) {
        return C4002_PARSE_ERR_CHECKSUM;
    }
    const uint16_t payload_len = rd_u16(&parser->buf[10]);
    /* payload_len includes the 4-byte cmd/resp/len header and must fit inside the frame. */
    if (payload_len < 4u || payload_len > total - 10u || payload_len - 4u > C4002_MAX_DATA) {
        return C4002_PARSE_ERR_LENGTH;
    }
    out->frame_type = parser->buf[7];
    out->cmd = parser->buf[8];
    out->resp_code = parser->buf[9];
    out->data_len = (uint16_t)(payload_len - 4u);
    memcpy(out->data, &parser->buf[12], out->data_len);
    return C4002_PARSE_FRAME;
}

size_t c4002_build_frame(uint8_t frame_type, uint8_t cmd, const uint8_t *data, uint16_t data_len,
                         uint8_t *out, size_t out_size)
{
    const size_t total = (size_t)data_len + 14u;
    if (out == NULL || out_size < total || (data_len > 0 && data == NULL)) {
        return 0;
    }
    const uint16_t payload_len = (uint16_t)(data_len + 4u);
    size_t n = 0;
    memcpy(out, HEADER, sizeof(HEADER));
    n += sizeof(HEADER);
    out[n++] = (uint8_t)(total & 0xFF);
    out[n++] = (uint8_t)(total >> 8);
    out[n++] = 0x00;
    out[n++] = frame_type;
    out[n++] = cmd;
    out[n++] = C4002_RESP_REQUEST;
    out[n++] = (uint8_t)(payload_len & 0xFF);
    out[n++] = (uint8_t)(payload_len >> 8);
    if (data_len > 0) {
        memcpy(&out[n], data, data_len);
        n += data_len;
    }
    const uint16_t sum = checksum(out, n);
    out[n++] = (uint8_t)(sum & 0xFF);
    out[n++] = (uint8_t)(sum >> 8);
    return n;
}

bool c4002_decode_result(const c4002_frame_t *frame, c4002_result_t *out)
{
    if (frame->frame_type != C4002_FRAME_NOTIFICATION || frame->cmd != C4002_NOTE_RESULT ||
        frame->data_len < C4002_RESULT_DATA_LEN) {
        return false;
    }
    /*
     * Packed layout, as parsed by DFRobot's Python driver. (The Arduino driver
     * memcpy()s into a naturally aligned struct, which shifts every field after
     * byte 0 on 32-bit MCUs.)
     */
    const uint8_t *d = frame->data;
    out->target_state = d[0];
    out->light_dlux = rd_u16(&d[1]);
    out->presence_gate_mask = (uint32_t)d[3] | ((uint32_t)d[4] << 8) | ((uint32_t)d[5] << 16) | ((uint32_t)d[6] << 24);
    out->presence_countdown_s = rd_u16(&d[7]);
    out->presence_distance_cm = rd_u16(&d[9]);
    out->presence_energy = d[11];
    out->motion_distance_cm = rd_u16(&d[12]);
    out->motion_speed_cm_s = (int16_t)rd_u16(&d[14]);
    out->motion_energy = d[16];
    out->motion_direction = d[17];
    return true;
}

void c4002_result_to_presence(const c4002_result_t *result, uint32_t timestamp_ms, presence_reading_t *out)
{
    memset(out, 0, sizeof(*out));
    out->timestamp_ms = timestamp_ms;
    out->distance_m = NAN;

    switch (result->target_state) {
    case C4002_TARGET_NONE:
        out->valid = true;
        return;
    case C4002_TARGET_PRESENCE:
        if (result->presence_distance_cm > C4002_MAX_PLAUSIBLE_CM) {
            return; /* invalid */
        }
        out->valid = true;
        out->presence_detected = true;
        out->stationary_target = true;
        out->distance_m = (float)result->presence_distance_cm / 100.0f;
        return;
    case C4002_TARGET_MOTION:
        if (result->motion_distance_cm > C4002_MAX_PLAUSIBLE_CM) {
            return; /* invalid */
        }
        out->valid = true;
        out->presence_detected = true;
        out->moving_target = true;
        out->distance_m = (float)result->motion_distance_cm / 100.0f;
        return;
    default:
        return; /* unknown state: invalid */
    }
}
