#pragma once

/*
 * Feeds the fault manager from the controller's subsystems and publishes
 * every change as an RB_EVT_FAULT event (TODO section 16).
 */
#include <stdint.h>
#include "safety.h"
#include "sensor_node.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Start the fault manager. The faults of every configured sensor node start
 * out active ("no data yet") and clear once that node delivers valid data.
 */
void rb_app_faults_init(const node_set_config_t *nodes);

/* rb_controller hook: map one node's health events onto its faults. */
void rb_app_faults_node_events(const sensor_node_state_t *node, node_slot_t slot, uint32_t events, void *ctx);

/* rb_controller hook: poll ESP-NOW statistics, queues and outputs for faults. */
void rb_app_faults_tick(uint32_t now_ms, void *ctx);

/* rb_controller hook: any SAFETY-class fault active? */
bool rb_app_faults_safety_active(void *ctx);

/* rb_controller hook: controller self-test (outputs must be working). */
safety_selftest_t rb_app_self_test(void *ctx);

#ifdef __cplusplus
}
#endif
