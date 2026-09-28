#pragma once

/*
 * ESP-NOW link task of a sensor node (TODO section 5). Every 50 ms it asks
 * the node's firmware for its latest reading and sends:
 *  - the data message (PRESENCE_DATA or THERMAL_DATA) every
 *    CONFIG_RB_NODE_DATA_PERIOD_MS,
 *  - HEARTBEAT every CONFIG_RB_NODE_HEARTBEAT_PERIOD_MS,
 *  - SENSOR_FAULT right away whenever a sensor fault flag changes.
 * Every packet carries CONFIG_RB_NODE_ID and the node's role.
 */
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "rb_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    rb_node_role_t role;
    /*
     * Fill data->type and data->body with the latest reading and return the
     * current rb_sensor_fault_t flags. Called from the link task; must not
     * block.
     */
    uint16_t (*sample)(rb_packet_t *data, void *ctx);
    /* One-line summary of a reading for the periodic log. */
    void (*describe)(const rb_packet_t *data, char *buf, size_t len);
    void *ctx;
} rb_node_link_config_t;

esp_err_t rb_node_link_start(const rb_node_link_config_t *config);

#ifdef __cplusplus
}
#endif
