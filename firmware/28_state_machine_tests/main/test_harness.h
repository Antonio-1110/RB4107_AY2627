#pragma once

/* Simulated-clock harness shared by the safety and integration tests. */
#include "safety.h"
#include "unity.h"

#define TH_STEP_MS 100u

typedef struct {
    safety_sm_t sm;
    safety_inputs_t in;
    safety_outputs_t out;
    uint32_t now;
} th_t;

safety_config_t th_default_config(void);

/* Init at t0 with a person present, a cold hob and self-test PASS; ends in IDLE. */
void th_start(th_t *h, const safety_config_t *cfg, uint32_t t0);

/* Step the machine every TH_STEP_MS for ms milliseconds. */
void th_run(th_t *h, uint32_t ms);

/* Heat the hob: IDLE -> MONITORING. */
void th_cook(th_t *h);

#define TH_EXPECT(h, expected) TEST_ASSERT_EQUAL_STRING(safety_state_name(expected), safety_state_name((h)->sm.state))
