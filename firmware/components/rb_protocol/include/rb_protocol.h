#pragma once

/*
 * RB4107 sensor-node -> controller protocol over ESP-NOW (TODO section 4),
 * plus the controller <-> valve node messages.
 *
 * The C6 sender and the S3 receiver both compile this one file, so they can't
 * drift apart. Packets are serialised field by field in little-endian order;
 * C structs are never sent as-is, so padding, alignment and compiler
 * differences can't change the wire format. The layout is documented in
 * docs/protocol.md.
 *
 * Any change to the layout must bump RB_PROTOCOL_VERSION.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rb_sensor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RB_PROTOCOL_VERSION 2u
#define RB_PROTOCOL_MAGIC 0x4252u /* "RB", sent as 0x52 0x42 */

/* ESP-NOW v1 payload limit (ESP_NOW_MAX_DATA_LEN). Checked again against the IDF header in rb_espnow. */
#define RB_ESPNOW_MAX_PAYLOAD 250u

/*
 * What a node measures. Every packet carries its sender's role, so the
 * controller can reject a node flashed with the wrong firmware or ID.
 */
typedef enum {
    RB_NODE_ROLE_PRESENCE = 1,   /* one C4002 radar (firmware/presence_node) */
    RB_NODE_ROLE_THERMAL = 2,    /* one MLX90640 (firmware/thermal_node) */
    RB_NODE_ROLE_VALVE = 3,      /* servo gas valve (firmware/valve_node, wireless mode) */
    RB_NODE_ROLE_CONTROLLER = 4, /* the S3, when it commands the valve node */
} rb_node_role_t;

typedef enum {
    RB_MSG_PRESENCE_DATA = 1,    /* presence nodes only */
    RB_MSG_THERMAL_DATA = 2,     /* thermal nodes only */
    RB_MSG_HEARTBEAT = 3,
    RB_MSG_SENSOR_FAULT = 4,
    RB_MSG_VALVE_COMMAND = 5,    /* controller -> valve node */
    RB_MSG_VALVE_STATUS = 6,     /* valve node -> controller */
} rb_msg_type_t;

/* VALVE_COMMAND. Any value other than KEEP_OPEN is treated as CLOSE. */
typedef enum {
    RB_VALVE_CMD_KEEP_OPEN = 1,
    RB_VALVE_CMD_CLOSE = 2,
} rb_valve_cmd_t;

/* Commanded valve position (a hobby servo has no position feedback). */
typedef enum {
    RB_VALVE_POS_OPEN = 1,
    RB_VALVE_POS_CLOSED = 2,
} rb_valve_pos_t;

/* Why the valve is closed. */
typedef enum {
    RB_VALVE_REASON_NONE = 0,          /* open */
    RB_VALVE_REASON_BOOT = 1,          /* closed since power-up, no keep-open yet */
    RB_VALVE_REASON_COMMAND = 2,       /* the controller sent CLOSE */
    RB_VALVE_REASON_LINK_TIMEOUT = 3,  /* no keep-open within the timeout */
    RB_VALVE_REASON_WIRED_LINE = 4,    /* wired mode: the input went high */
} rb_valve_reason_t;

/* VALVE_STATUS flags. */
enum {
    RB_VALVE_FLAG_MOVING = 1u << 0,    /* the servo may still be travelling */
    RB_VALVE_FLAG_LATCHED = 1u << 1,   /* stays closed until the valve node is reset */
};

/* Sensor fault / validity flags, used by heartbeat and fault messages. */
typedef enum {
    RB_FAULT_C4002_NO_DATA = 1u << 0,   /* no report from the C4002 (unplugged, dead, wrong pins) */
    RB_FAULT_C4002_INVALID = 1u << 1,   /* reports arrive but fail validation */
    RB_FAULT_MLX_NO_DATA = 1u << 2,     /* MLX90640 not responding / init failed */
    RB_FAULT_MLX_INVALID = 1u << 3,     /* frames arrive but fail validation */
} rb_sensor_fault_t;

/* Sizes on the wire. */
#define RB_HEADER_LEN 17u
#define RB_CRC_LEN 2u
#define RB_BODY_PRESENCE_DATA_LEN 7u
#define RB_BODY_THERMAL_DATA_LEN 17u
#define RB_BODY_HEARTBEAT_LEN 10u
#define RB_BODY_SENSOR_FAULT_LEN 8u
#define RB_BODY_VALVE_COMMAND_LEN 5u
#define RB_BODY_VALVE_STATUS_LEN 7u
#define RB_PKT_PRESENCE_DATA_LEN (RB_HEADER_LEN + RB_BODY_PRESENCE_DATA_LEN + RB_CRC_LEN)
#define RB_PKT_THERMAL_DATA_LEN (RB_HEADER_LEN + RB_BODY_THERMAL_DATA_LEN + RB_CRC_LEN)
#define RB_PKT_HEARTBEAT_LEN (RB_HEADER_LEN + RB_BODY_HEARTBEAT_LEN + RB_CRC_LEN)
#define RB_PKT_SENSOR_FAULT_LEN (RB_HEADER_LEN + RB_BODY_SENSOR_FAULT_LEN + RB_CRC_LEN)
#define RB_PKT_VALVE_COMMAND_LEN (RB_HEADER_LEN + RB_BODY_VALVE_COMMAND_LEN + RB_CRC_LEN)
#define RB_PKT_VALVE_STATUS_LEN (RB_HEADER_LEN + RB_BODY_VALVE_STATUS_LEN + RB_CRC_LEN)
#define RB_PKT_MAX_LEN RB_PKT_THERMAL_DATA_LEN

_Static_assert(RB_PKT_MAX_LEN <= RB_ESPNOW_MAX_PAYLOAD, "packet exceeds ESP-NOW payload");
_Static_assert(RB_PKT_MAX_LEN >= RB_PKT_PRESENCE_DATA_LEN && RB_PKT_MAX_LEN >= RB_PKT_HEARTBEAT_LEN &&
                   RB_PKT_MAX_LEN >= RB_PKT_SENSOR_FAULT_LEN && RB_PKT_MAX_LEN >= RB_PKT_VALVE_COMMAND_LEN &&
                   RB_PKT_MAX_LEN >= RB_PKT_VALVE_STATUS_LEN,
               "RB_PKT_MAX_LEN must cover every message");

typedef struct {
    uint16_t fault_flags;      /* rb_sensor_fault_t bits currently active */
    uint32_t tx_ok;
    uint32_t tx_fail;
} rb_heartbeat_t;

typedef struct {
    uint16_t fault_flags;      /* all currently active faults */
    uint16_t changed_flags;    /* bits that changed and triggered this message */
    int32_t detail;            /* driver error code, 0 if none */
} rb_sensor_fault_msg_t;

typedef struct {
    rb_valve_cmd_t command;
    uint32_t valve_node_id;    /* RB_NODE_ID of the valve node this is meant for */
} rb_valve_command_t;

typedef struct {
    rb_valve_pos_t position;
    uint8_t flags;             /* RB_VALVE_FLAG_* */
    rb_valve_reason_t reason;
    uint32_t last_command_seq; /* sequence of the last VALVE_COMMAND accepted, 0 if none */
} rb_valve_status_t;

typedef struct {
    uint8_t protocol_version;
    rb_msg_type_t type;
    rb_node_role_t role;
    uint32_t node_id;
    uint32_t sequence;
    uint32_t uptime_ms;
    union {
        presence_reading_t presence;   /* RB_MSG_PRESENCE_DATA */
        thermal_reading_t thermal;     /* RB_MSG_THERMAL_DATA */
        rb_heartbeat_t heartbeat;
        rb_sensor_fault_msg_t fault;
        rb_valve_command_t valve_command;
        rb_valve_status_t valve_status;
    } body;
} rb_packet_t;

typedef enum {
    RB_DECODE_OK = 0,
    RB_DECODE_ERR_LENGTH,     /* too short, or wrong length for the message type */
    RB_DECODE_ERR_MAGIC,
    RB_DECODE_ERR_VERSION,
    RB_DECODE_ERR_TYPE,
    RB_DECODE_ERR_CRC,
    RB_DECODE_ERR_ROLE,       /* unknown role, or a data message that doesn't match the role */
} rb_decode_result_t;

/* Serialise pkt into buf. Returns the number of bytes written, 0 on error. */
size_t rb_protocol_encode(const rb_packet_t *pkt, uint8_t *buf, size_t buf_len);

/* Validate and parse a received buffer. */
rb_decode_result_t rb_protocol_decode(const uint8_t *buf, size_t len, rb_packet_t *out);

const char *rb_decode_result_name(rb_decode_result_t result);
const char *rb_msg_type_name(rb_msg_type_t type);
const char *rb_node_role_name(rb_node_role_t role); /* "presence" / "thermal" / "valve" / "controller" */

/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF). */
uint16_t rb_crc16(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
