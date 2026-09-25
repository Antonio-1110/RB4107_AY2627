#pragma once

/*
 * RB4107 MQTT payloads (TODO section 21). The schema is documented in
 * docs/mqtt_schema.md, and the machine-readable JSON Schema is in docs/schema/
 * (the Django subscriber validates against those same files).
 *
 * Every message has: schema_version, type, controller_id, timestamp (ISO 8601
 * or null while the wall clock is unset), uptime_ms and sequence. Missing
 * values are null, never a made-up number.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RB_JSON_SCHEMA_VERSION 1

typedef struct {
    const char *controller_id;
    const char *timestamp;        /* NULL -> null */
    uint32_t uptime_ms;
    uint32_t sequence;            /* per-controller message counter */
} rb_json_header_t;

typedef struct {
    const char *name;
    const char *fault_class;      /* "SAFETY" / "TELEMETRY" */
} rb_json_fault_t;

typedef struct {
    rb_json_header_t hdr;
    uint8_t protocol_version;     /* ESP-NOW protocol version of the node data */

    const char *sensor_node;      /* "node_01" */
    struct {
        const char *link;         /* NEVER_SEEN / ONLINE / STALE / OFFLINE */
        uint32_t missed;
        uint32_t restarts;
        uint16_t fault_flags;     /* rb_sensor_fault_t bits reported by the node */
    } node;

    struct {
        bool valid;
        bool detected;
        bool moving;
        bool stationary;
        float distance_m;
    } presence;

    struct {
        bool valid;
        float max_c;
        float min_c;
        float mean_c;
        float hot_region_c;
        float rate_c_per_min;
        uint16_t pixels_above_threshold;
    } thermal;

    struct {
        const char *state;
        uint32_t state_ms;
        uint32_t unattended_ms;
        const char *buzzer;
        bool shutdown;
        bool test_timers;
        uint32_t loop_count;
    } safety;

    const rb_json_fault_t *faults;
    size_t fault_count;
} rb_telemetry_t;

typedef struct {
    rb_json_header_t hdr;
    const char *event;            /* state_change, warning, shutdown, fault_raised, fault_cleared */
    const char *state;            /* safety state after the event */
    const char *from_state;       /* state_change/warning/shutdown only, else NULL */
    const char *reason;
    uint32_t unattended_ms;
    const char *fault;            /* fault events only, else NULL */
    const char *fault_class;
} rb_event_msg_t;

/* Each returns the JSON length, or 0 if it did not fit into buf. */
size_t rb_json_telemetry(const rb_telemetry_t *t, char *buf, size_t len);   /* controller/state */
size_t rb_json_heartbeat(const rb_telemetry_t *t, char *buf, size_t len);   /* controller/heartbeat */
size_t rb_json_faults(const rb_telemetry_t *t, char *buf, size_t len);      /* controller/faults */
size_t rb_json_presence(const rb_telemetry_t *t, char *buf, size_t len);    /* sensors/<node>/presence */
size_t rb_json_thermal(const rb_telemetry_t *t, char *buf, size_t len);     /* sensors/<node>/thermal */
size_t rb_json_node_status(const rb_telemetry_t *t, char *buf, size_t len); /* sensors/<node>/status */
size_t rb_json_event(const rb_event_msg_t *e, char *buf, size_t len);       /* events/... */

/* controller/status (retained) and the MQTT Last Will. */
size_t rb_json_controller_status(const char *controller_id, bool online, char *buf, size_t len);

#ifdef __cplusplus
}
#endif
