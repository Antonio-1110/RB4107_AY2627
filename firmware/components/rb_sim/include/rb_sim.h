#pragma once

/*
 * Simulated sensor node (TODO section 27: "simulated sensor inputs").
 *
 * Builds real rb_protocol packets (SENSOR_DATA + HEARTBEAT) from the values
 * set here and injects them into the controller's receive queue. So the
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

typedef struct {
    bool node_online;          /* false: stop sending (node disappears) */
    bool presence_valid;       /* false: C4002 unavailable */
    bool person_present;
    bool thermal_valid;        /* false: MLX90640 unavailable */
    float hot_region_c;
    float rate_c_per_min;
} rb_sim_inputs_t;

/* A scripted step: at at_ms (from script start) apply inputs; optionally press reset. */
typedef struct {
    uint32_t at_ms;
    const char *label;
    rb_sim_inputs_t inputs;
    bool press_reset;
} rb_sim_step_t;

/* Start sending simulated packets for node_id every period_ms. */
esp_err_t rb_sim_start(uint32_t node_id, uint32_t period_ms);

void rb_sim_set(const rb_sim_inputs_t *inputs);
void rb_sim_get(rb_sim_inputs_t *out);

/* Play a script in the background (loops if loop is true). */
esp_err_t rb_sim_run_script(const rb_sim_step_t *steps, size_t count, bool loop);

/* Defaults: node online, person present, both sensors valid, room temperature. */
rb_sim_inputs_t rb_sim_defaults(void);

#ifdef __cplusplus
}
#endif
