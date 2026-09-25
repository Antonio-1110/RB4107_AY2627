#pragma once

/*
 * DFRobot C4002 (SEN0691) 24 GHz mmWave presence sensor: UART frame codec.
 *
 * Ported from the official DFRobot_C4002 library
 * (https://github.com/DFRobot/DFRobot_C4002, MIT). Frame layout, all
 * multi-byte fields little-endian:
 *
 *   FA F5 AA A5 | total_len:u16 | 00 | frame_type | cmd | resp | data_len:u16 | data... | sum:u16
 *
 * total_len is the length of the whole frame. data_len counts the 4-byte
 * cmd/resp/data_len header plus the data. sum is the 16-bit sum of every
 * byte before it.
 *
 * Plain C with no ESP-IDF dependency, so it can be unit tested on a host.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rb_sensor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define C4002_FRAME_MIN 14u /* 8 header + 4 cmd/resp/data_len + 2 checksum */
#define C4002_MAX_DATA 64u
#define C4002_MAX_FRAME (C4002_MAX_DATA + 14u)

/* Frame types */
#define C4002_FRAME_WRITE_REQUEST 0x00
#define C4002_FRAME_READ_REQUEST 0x01
#define C4002_FRAME_WRITE_RESPONSE 0x02
#define C4002_FRAME_READ_RESPONSE 0x03
#define C4002_FRAME_NOTIFICATION 0x04

/* Commands */
#define C4002_CMD_RESTART 0x00
#define C4002_CMD_FACTORY_RESET_USER 0x02
#define C4002_CMD_SET_BAUDRATE 0x21
#define C4002_CMD_ENV_CALIBRATION 0x60
#define C4002_CMD_DISTANCE_GATE 0x62
#define C4002_CMD_DISTANCE_GATE_THRESHOLD 0x63
#define C4002_CMD_RESOLUTION_MODE 0x66
#define C4002_CMD_FACTORY_RESET 0x80
#define C4002_CMD_GET_VERSION 0x82
#define C4002_CMD_REPORT_PERIOD 0x83
#define C4002_CMD_TARGET_DISAPPEAR_DELAY 0x84
#define C4002_CMD_LOCK_TIME 0x85
#define C4002_CMD_DETECT_RANGE 0x86
#define C4002_CMD_THRESHOLD_GROUP 0x87
#define C4002_CMD_LIGHT_THRESHOLD 0x88
#define C4002_CMD_OUT_MODE 0xA0
#define C4002_CMD_LED_MODE 0xA1

/* Notification commands */
#define C4002_NOTE_RESULT 0x60
#define C4002_NOTE_CALIBRATION 0x03

/* Response codes */
#define C4002_RESP_REQUEST 0x00
#define C4002_RESP_OK 0x01

typedef enum {
    C4002_TARGET_NONE = 0,
    C4002_TARGET_PRESENCE = 1, /* stationary target */
    C4002_TARGET_MOTION = 2,
} c4002_target_state_t;

typedef struct {
    uint8_t frame_type;
    uint8_t cmd;
    uint8_t resp_code;
    uint16_t data_len;              /* number of bytes in data[] (header excluded) */
    uint8_t data[C4002_MAX_DATA];
} c4002_frame_t;

/* Decoded detection-result notification (cmd 0x60, 18 data bytes, packed). */
typedef struct {
    uint8_t target_state;           /* c4002_target_state_t */
    uint16_t light_dlux;            /* 0.1 lux */
    uint32_t presence_gate_mask;    /* distance gates currently holding a stationary target */
    uint16_t presence_countdown_s;
    uint16_t presence_distance_cm;
    uint8_t presence_energy;
    uint16_t motion_distance_cm;
    int16_t motion_speed_cm_s;
    uint8_t motion_energy;
    uint8_t motion_direction;       /* 0 away, 1 none, 2 approaching */
} c4002_result_t;

#define C4002_RESULT_DATA_LEN 18u

typedef enum {
    C4002_PARSE_NONE = 0,       /* need more bytes */
    C4002_PARSE_FRAME,          /* *out holds a complete, checksum-valid frame */
    C4002_PARSE_ERR_CHECKSUM,
    C4002_PARSE_ERR_LENGTH,
} c4002_parse_status_t;

typedef struct {
    uint8_t buf[C4002_MAX_FRAME];
    uint16_t len;
    uint16_t expected;
} c4002_parser_t;

void c4002_parser_reset(c4002_parser_t *parser);

/* Feed one received byte. Resynchronises on the frame header after any error. */
c4002_parse_status_t c4002_parser_feed(c4002_parser_t *parser, uint8_t byte, c4002_frame_t *out);

/* Build a request frame into out. Returns the frame length, or 0 if it does not fit. */
size_t c4002_build_frame(uint8_t frame_type, uint8_t cmd, const uint8_t *data, uint16_t data_len,
                         uint8_t *out, size_t out_size);

/* Decode a detection-result notification. Returns false if the frame is not one. */
bool c4002_decode_result(const c4002_frame_t *frame, c4002_result_t *out);

/*
 * Convert a decoded result into the project-wide presence reading.
 * Sanity checks (unknown target state, distance beyond the sensor's range)
 * mark the reading invalid instead of guessing.
 */
void c4002_result_to_presence(const c4002_result_t *result, uint32_t timestamp_ms, presence_reading_t *out);

#ifdef __cplusplus
}
#endif
