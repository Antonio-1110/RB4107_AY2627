#pragma once

/*
 * Central fault handling (TODO section 16).
 *
 * Every fault has one entry in a table giving its name and its class:
 *
 *   SAFETY     affects whether the controller can protect the kitchen. Any
 *              active one puts the state machine into FAULT.
 *   TELEMETRY  affects reporting/diagnostics only. It is logged and published,
 *              and must never change the safety state (e.g. MQTT disconnected).
 */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /* One per sensor node; the order matches node_slot_t (A, B, thermal). */
    FAULT_PRESENCE_A_UNAVAILABLE = 0,  /* presence node A: C4002 reading missing/invalid */
    FAULT_PRESENCE_B_UNAVAILABLE,      /* presence node B: C4002 reading missing/invalid */
    FAULT_THERMAL_UNAVAILABLE,         /* thermal node: MLX90640 reading missing/invalid */
    FAULT_PRESENCE_A_NODE_OFFLINE,     /* no packets from presence node A */
    FAULT_PRESENCE_B_NODE_OFFLINE,     /* no packets from presence node B */
    FAULT_THERMAL_NODE_OFFLINE,        /* no packets from the thermal node */
    FAULT_SHUTDOWN_OUTPUT,         /* relay expander failed to write/verify */
    FAULT_SELF_TEST,               /* controller self-test failed */
    FAULT_ESPNOW_INVALID_PACKET,   /* malformed/foreign packets received */
    FAULT_ESPNOW_LINK_DEGRADED,    /* a node is STALE */
    FAULT_ESPNOW_UNKNOWN_NODE,     /* packets from an unconfigured node ID, or a node with the wrong role */
    FAULT_RTC,                     /* RTC unavailable: timestamps only */
    FAULT_NETWORK_DOWN,
    FAULT_MQTT_DOWN,
    FAULT_RX_QUEUE_OVERFLOW,       /* ESP-NOW packets dropped before the safety task saw them */
    FAULT_EVENT_QUEUE_OVERFLOW,    /* telemetry events dropped */
    FAULT_COUNT,
} fault_id_t;

typedef enum {
    FAULT_CLASS_SAFETY = 0,
    FAULT_CLASS_TELEMETRY,
} fault_class_t;

typedef struct {
    const char *name;
    fault_class_t fault_class;
} fault_info_t;

typedef struct {
    bool active;
    uint32_t since_ms;       /* monotonic time of the last raise/clear */
    uint32_t raise_count;
    int32_t detail;          /* last detail code given to fault_raise */
} fault_status_t;

/* Called outside the lock on every change. Keep it short and non-blocking. */
typedef void (*fault_change_cb_t)(fault_id_t id, bool active, int32_t detail, void *ctx);

/* now_fn supplies monotonic milliseconds (so the module can be tested without hardware). */
void fault_manager_init(uint32_t (*now_fn)(void), fault_change_cb_t cb, void *ctx);

void fault_raise(fault_id_t id, int32_t detail);
void fault_clear(fault_id_t id);
void fault_set(fault_id_t id, bool active, int32_t detail);

bool fault_is_active(fault_id_t id);
uint32_t fault_active_mask(void);          /* bit n = fault_id_t n */
bool fault_any_safety_active(void);
void fault_get_status(fault_id_t id, fault_status_t *out);

const fault_info_t *fault_info(fault_id_t id);
const char *fault_class_name(fault_class_t c);

#ifdef __cplusplus
}
#endif
