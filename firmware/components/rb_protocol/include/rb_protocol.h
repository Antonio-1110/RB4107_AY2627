#pragma once

/*
 * RB4107 sensor-node -> controller protocol over ESP-NOW (TODO section 4),
 * plus the controller -> presence node C4002 tuning messages.
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
#include "rb_c4002_params.h"
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
    RB_NODE_ROLE_CONTROLLER = 4, /* the S3, when it sends a command to a node (3 is the valve node) */
} rb_node_role_t;

typedef enum {
    RB_MSG_PRESENCE_DATA = 1,    /* presence nodes only */
    RB_MSG_THERMAL_DATA = 2,     /* thermal nodes only */
    RB_MSG_HEARTBEAT = 3,
    RB_MSG_SENSOR_FAULT = 4,
    /* 5 and 6 are kept for the valve node messages. */
    RB_MSG_C4002_CONFIG = 7,     /* controller -> presence node */
    RB_MSG_C4002_CONFIG_ACK = 8, /* presence node -> controller */
    RB_MSG_C4002_LIVE = 9,       /* presence node -> controller: raw C4002 result, for the dashboard */
} rb_msg_type_t;

/* C4002_CONFIG actions. */
typedef enum {
    RB_C4002_ACTION_APPLY = 1,      /* change the fields in field_mask, save them */
    RB_C4002_ACTION_CALIBRATE = 2,  /* environment calibration, then keep the learned thresholds */
    RB_C4002_ACTION_READ = 3,       /* only report the current settings */
    RB_C4002_ACTION_RESET = 4,      /* forget saved settings, back to the menuconfig values */
} rb_c4002_action_t;

/* C4002_CONFIG field_mask bits (APPLY): which fields of params to change. */
enum {
    RB_C4002_F_REPORT_PERIOD = 1u << 0,
    RB_C4002_F_RANGE_MIN = 1u << 1,
    RB_C4002_F_RANGE_MAX = 1u << 2,
    RB_C4002_F_RESOLUTION = 1u << 3,
    RB_C4002_F_MOTION_SENS = 1u << 4,
    RB_C4002_F_PRESENCE_SENS = 1u << 5,
    RB_C4002_F_DISAPPEAR_DELAY = 1u << 6,
    RB_C4002_F_LOCK_TIME = 1u << 7,
    RB_C4002_F_MOTION_GATES = 1u << 8,
    RB_C4002_F_PRESENCE_GATES = 1u << 9,
    RB_C4002_F_MOTION_THRESH = 1u << 10,    /* also switches motion sensitivity to CUSTOM */
    RB_C4002_F_PRESENCE_THRESH = 1u << 11,  /* also switches presence sensitivity to CUSTOM */
    RB_C4002_F_ALL = (1u << 12) - 1,
};

/* C4002_CONFIG_ACK result codes. */
typedef enum {
    RB_C4002_RESULT_OK = 0,
    RB_C4002_RESULT_INVALID = 1,       /* rejected: a value is out of range; nothing changed */
    RB_C4002_RESULT_SENSOR_ERROR = 2,  /* the C4002 did not accept a command */
} rb_c4002_result_t;

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
#define RB_C4002_PARAMS_LEN (20u + 2u * RB_C4002_MAX_GATES)
#define RB_BODY_C4002_CONFIG_LEN (13u + RB_C4002_PARAMS_LEN)
#define RB_BODY_C4002_CONFIG_ACK_LEN (7u + RB_C4002_PARAMS_LEN)
#define RB_BODY_C4002_LIVE_LEN 27u
#define RB_PKT_PRESENCE_DATA_LEN (RB_HEADER_LEN + RB_BODY_PRESENCE_DATA_LEN + RB_CRC_LEN)
#define RB_PKT_THERMAL_DATA_LEN (RB_HEADER_LEN + RB_BODY_THERMAL_DATA_LEN + RB_CRC_LEN)
#define RB_PKT_HEARTBEAT_LEN (RB_HEADER_LEN + RB_BODY_HEARTBEAT_LEN + RB_CRC_LEN)
#define RB_PKT_SENSOR_FAULT_LEN (RB_HEADER_LEN + RB_BODY_SENSOR_FAULT_LEN + RB_CRC_LEN)
#define RB_PKT_C4002_CONFIG_LEN (RB_HEADER_LEN + RB_BODY_C4002_CONFIG_LEN + RB_CRC_LEN)
#define RB_PKT_C4002_CONFIG_ACK_LEN (RB_HEADER_LEN + RB_BODY_C4002_CONFIG_ACK_LEN + RB_CRC_LEN)
#define RB_PKT_C4002_LIVE_LEN (RB_HEADER_LEN + RB_BODY_C4002_LIVE_LEN + RB_CRC_LEN)
#define RB_PKT_MAX_LEN RB_PKT_C4002_CONFIG_LEN

_Static_assert(RB_PKT_MAX_LEN <= RB_ESPNOW_MAX_PAYLOAD, "packet exceeds ESP-NOW payload");
_Static_assert(RB_PKT_MAX_LEN >= RB_PKT_PRESENCE_DATA_LEN && RB_PKT_MAX_LEN >= RB_PKT_HEARTBEAT_LEN &&
                   RB_PKT_MAX_LEN >= RB_PKT_SENSOR_FAULT_LEN && RB_PKT_MAX_LEN >= RB_PKT_THERMAL_DATA_LEN &&
                   RB_PKT_MAX_LEN >= RB_PKT_C4002_CONFIG_ACK_LEN && RB_PKT_MAX_LEN >= RB_PKT_C4002_LIVE_LEN,
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
    uint32_t target_node_id;   /* RB_NODE_ID of the presence node this is for */
    uint16_t request_id;       /* chosen by the sender, echoed in the ACK */
    uint8_t action;            /* rb_c4002_action_t */
    uint16_t field_mask;       /* APPLY: RB_C4002_F_* bits of params to change */
    uint16_t calib_delay_s;    /* CALIBRATE: wait before starting (time to leave the area) */
    uint16_t calib_duration_s; /* CALIBRATE: how long the sensor learns the empty room */
    rb_c4002_params_t params;  /* APPLY: new values (only the masked fields are used) */
} rb_c4002_config_msg_t;

typedef struct {
    uint16_t request_id;       /* of the command this answers; 0 = the node reported on its own */
    uint8_t action;            /* rb_c4002_action_t it answers */
    uint8_t result;            /* rb_c4002_result_t */
    uint16_t calib_remaining_s;/* > 0 while an environment calibration is running */
    uint8_t saved;             /* 1 if the settings are saved on the node (survive a reboot) */
    rb_c4002_params_t params;  /* the settings now in use */
} rb_c4002_ack_t;

/* C4002_LIVE: one detection result as the sensor reported it (no filtering). */
typedef struct {
    uint8_t target_state;          /* 0 none, 1 stationary, 2 moving (the sensor's own verdict) */
    uint8_t resolution;            /* rb_c4002_resolution_t in use, gives the gate size */
    uint32_t presence_gate_mask;   /* bit i: gate i holds a stationary target */
    uint16_t presence_countdown_s; /* sensor's hold countdown before it reports empty */
    uint16_t presence_distance_cm;
    uint8_t presence_energy;       /* 0-99 */
    uint16_t motion_distance_cm;
    int16_t motion_speed_cm_s;
    uint8_t motion_energy;         /* 0-99 */
    uint8_t motion_direction;      /* 0 away, 1 none, 2 approaching */
    uint16_t light_dlux;           /* 0.1 lux */
    uint16_t calib_remaining_s;    /* > 0 while an environment calibration runs */
    uint16_t age_ms;               /* how old the result was when sent */
    uint32_t results;              /* results received from the sensor since boot */
} rb_c4002_live_t;

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
        rb_c4002_config_msg_t c4002_config;  /* RB_MSG_C4002_CONFIG */
        rb_c4002_ack_t c4002_ack;            /* RB_MSG_C4002_CONFIG_ACK */
        rb_c4002_live_t c4002_live;          /* RB_MSG_C4002_LIVE */
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
const char *rb_node_role_name(rb_node_role_t role); /* "presence" / "thermal" / "controller" */

/*
 * Check every C4002 parameter against the sensor's limits. Returns NULL if
 * all are valid, otherwise a short reason ("range_max_cm > 1100").
 */
const char *rb_c4002_params_check(const rb_c4002_params_t *params);

/* Copy the fields selected by field_mask from src into dst (thresholds switch to CUSTOM). */
void rb_c4002_params_merge(rb_c4002_params_t *dst, const rb_c4002_params_t *src, uint16_t field_mask);

const char *rb_c4002_action_name(uint8_t action);   /* "apply" / "calibrate" / "read" / "reset" */
const char *rb_c4002_result_name(uint8_t result);   /* "ok" / "invalid" / "sensor_error" */

/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF). */
uint16_t rb_crc16(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
