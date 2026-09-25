/*
 * TODO section 9: safety state machine.
 *
 * Runs the hardware-independent state machine against a scripted sequence of
 * simulated inputs on a simulated clock, and logs every transition with its
 * reason and the outputs the state asks for. No sensors, MQTT or network are
 * involved. The run takes milliseconds and prints the whole story.
 */
#include <inttypes.h>
#include <stdio.h>
#include "esp_log.h"
#include "safety.h"

static const char *TAG = "SAFETY";

#define STEP_MS 100u

typedef struct {
    uint32_t at_ms;
    const char *what;
    void (*apply)(safety_inputs_t *in);
} scenario_step_t;

static void selftest_pass(safety_inputs_t *in) { in->self_test = SAFETY_SELFTEST_PASS; }
static void stove_on(safety_inputs_t *in) { in->hot_region_temp_c = 80.0f; }
static void person_leaves(safety_inputs_t *in) { in->presence = RB_FALSE; }
static void person_returns(safety_inputs_t *in) { in->presence = RB_TRUE; }
static void press_reset(safety_inputs_t *in) { in->reset_request = true; }
static void release_reset(safety_inputs_t *in) { in->reset_request = false; }
static void thermal_lost(safety_inputs_t *in) { in->thermal_valid = false; }
static void thermal_back(safety_inputs_t *in) { in->thermal_valid = true; }
static void stove_off(safety_inputs_t *in) { in->hot_region_temp_c = 30.0f; }

static const scenario_step_t SCENARIO[] = {
    {500, "self-test passes", selftest_pass},
    {2000, "stove turned on (80 C)", stove_on},
    {5000, "person walks away", person_leaves},
    {20000, "person returns", person_returns},
    {20500, "operator presses reset", press_reset},
    {20600, "reset released", release_reset},
    {25000, "MLX90640 data lost", thermal_lost},
    {27000, "MLX90640 data back", thermal_back},
    {30000, "stove turned off (30 C)", stove_off},
};

static void on_transition(safety_state_t from, safety_state_t to, const char *reason, uint32_t now_ms, void *ctx)
{
    const safety_sm_t *sm = ctx;
    ESP_LOGI(TAG, "t=%6.1fs  %-10s -> %-10s (%s)  buzzer=%s shutdown=%s", now_ms / 1000.0, safety_state_name(from),
             safety_state_name(to), reason, safety_buzzer_name(sm->outputs.buzzer), sm->outputs.shutdown ? "ON" : "off");
}

void app_main(void)
{
    /* Short demo timings; real defaults come from menuconfig (section 10). */
    const safety_config_t cfg = {
        .self_test_timeout_ms = 3000,
        .heat_on_temp_c = 50.0f,
        .heat_off_temp_c = 40.0f,
        .absence_debounce_ms = 1000,
        .presence_return_debounce_ms = 0,
        .warning_timeout_ms = 5000,
        .shutdown_timeout_ms = 10000,
        .shutdown_timing = SAFETY_SHUTDOWN_AFTER_UNATTENDED_START,
        .warning_exit = SAFETY_WARNING_EXIT_ON_PRESENCE,
        .cooling_policy = SAFETY_COOLING_KEEP_TIMERS,
        .fault_shutdown_timeout_ms = 10000,
    };
    if (!safety_config_valid(&cfg)) {
        ESP_LOGE(TAG, "invalid configuration");
        return;
    }

    static safety_sm_t sm;
    safety_init(&sm, &cfg, on_transition, &sm, 0);
    safety_inputs_t in = {
        .presence = RB_TRUE,
        .thermal_valid = true,
        .hot_region_temp_c = 25.0f,
        .temp_rate_c_per_min = 0.0f,
        .self_test = SAFETY_SELFTEST_PENDING,
    };

    ESP_LOGI(TAG, "simulated run: warning after 5 s unattended, shutdown after 10 s unattended");
    size_t next = 0;
    for (uint32_t t = 0; t <= 35000; t += STEP_MS) {
        while (next < sizeof(SCENARIO) / sizeof(SCENARIO[0]) && SCENARIO[next].at_ms <= t) {
            ESP_LOGI(TAG, "t=%6.1fs  input: %s", t / 1000.0, SCENARIO[next].what);
            SCENARIO[next].apply(&in);
            next++;
        }
        safety_step(&sm, &in, t);
    }
    ESP_LOGI(TAG, "done: %" PRIu32 " transitions, final state %s", sm.transitions, safety_state_name(sm.state));
}
