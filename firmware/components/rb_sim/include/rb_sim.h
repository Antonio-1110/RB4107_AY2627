#pragma once

/*
 * Simulated sensor nodes (TODO section 27: "simulated sensor inputs").
 *
 * Plays every configured node (two presence nodes, one thermal node). It
 * builds real rb_protocol packets (PRESENCE_DATA / THERMAL_DATA + HEARTBEAT)
 * from the values set here and injects them into the controller's receive
 * queue. So the
 * packets go through the same sequence tracking, node health, state machine
 * and telemetry as real ESP-NOW data. There is no separate "test logic".
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "rb_sensor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Node slots, same order as the controller's node_slot_t. */
typedef enum {
    RB_SIM_PRESENCE_A = 0,
    RB_SIM_PRESENCE_B,
    RB_SIM_THERMAL,
    RB_SIM_NODE_COUNT,
} rb_sim_node_t;

typedef struct {
    bool online[RB_SIM_NODE_COUNT];   /* false: that node stops sending (disappears) */
    bool valid[RB_SIM_NODE_COUNT];    /* false: that node's sensor reports invalid */
    bool person_present;              /* what every valid presence node sees */
    float hot_region_c;
    float rate_c_per_min;
} rb_sim_inputs_t;

/*
 * Start sending simulated packets every period_ms for the configured nodes
 * (presence_ids[0..presence_count-1] and thermal_id).
 */
esp_err_t rb_sim_start(const uint32_t presence_ids[2], uint8_t presence_count, uint32_t thermal_id, uint32_t period_ms);

void rb_sim_set(const rb_sim_inputs_t *inputs);
void rb_sim_get(rb_sim_inputs_t *out);

/* Defaults: every node online and valid, person present, room temperature. */
rb_sim_inputs_t rb_sim_defaults(void);

#ifdef __cplusplus
}
#endif
