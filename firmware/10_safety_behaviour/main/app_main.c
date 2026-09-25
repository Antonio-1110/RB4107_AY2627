/*
 * TODO section 10: initial safety behaviour.
 *
 * Checks each required behaviour against the timings and policies set in
 * menuconfig, using the real state machine on a simulated clock:
 *
 *   IDLE -> MONITORING          cooking detected from the thermal reading
 *   MONITORING -> UNATTENDED    person absent (after debounce), timer starts
 *   UNATTENDED -> MONITORING    person returns, timer cancelled
 *   UNATTENDED -> WARNING       warning timeout (default 60 s)
 *   WARNING -> SHUTDOWN         shutdown timeout (default 90 s total)
 *   temperature interaction     timing hook shortens the timers
 *
 * Every line prints PASS or FAIL; the summary is at the end.
 */
#include <inttypes.h>
#include <math.h>
#include "esp_log.h"
#include "rb_safety_config.h"
#include "safety.h"

static const char *TAG = "SAFETY";

#define STEP_MS 100u

typedef struct {
    safety_sm_t sm;
    safety_inputs_t in;
    uint32_t now;
} sim_t;

static int s_pass, s_fail;

static void on_transition(safety_state_t from, safety_state_t to, const char *reason, uint32_t now_ms, void *ctx)
{
    ESP_LOGI(TAG, "    t=%7.1fs %s -> %s (%s)", now_ms / 1000.0, safety_state_name(from), safety_state_name(to), reason);
}

static void run(sim_t *s, uint32_t ms)
{
    for (uint32_t end = s->now + ms; s->now < end;) {
        s->now += STEP_MS;
        safety_step(&s->sm, &s->in, s->now);
    }
}

static void expect(const sim_t *s, safety_state_t state, const char *what)
{
    const bool ok = s->sm.state == state;
    ok ? s_pass++ : s_fail++;
    if (ok) {
        ESP_LOGI(TAG, "PASS  %s", what);
    } else {
        ESP_LOGE(TAG, "FAIL  %s: expected %s, got %s", what, safety_state_name(state), safety_state_name(s->sm.state));
    }
}

/* Start in MONITORING: person present, stove hot. */
static void start_monitoring(sim_t *s, const safety_config_t *cfg)
{
    safety_init(&s->sm, cfg, on_transition, NULL, 0);
    s->now = 0;
    s->in = (safety_inputs_t){
        .presence = RB_TRUE,
        .thermal_valid = true,
        .hot_region_temp_c = cfg->heat_on_temp_c - 10.0f,
        .temp_rate_c_per_min = 0.0f,
        .self_test = SAFETY_SELFTEST_PASS,
    };
    run(s, STEP_MS);
    expect(s, SAFETY_IDLE, "boot -> IDLE with a cold stove");
    s->in.hot_region_temp_c = cfg->heat_on_temp_c + 20.0f;
    run(s, STEP_MS);
    expect(s, SAFETY_MONITORING, "IDLE -> MONITORING when the hot region passes the cooking threshold");
}

/* Time from the person leaving until the shutdown is due. */
static uint32_t shutdown_after_leaving(const safety_config_t *cfg)
{
    return cfg->shutdown_timing == SAFETY_SHUTDOWN_AFTER_UNATTENDED_START ? cfg->shutdown_timeout_ms
                                                                          : cfg->warning_timeout_ms + cfg->shutdown_timeout_ms;
}

/* Example trend hook: halve both timeouts while the pan heats fast. */
static void fast_heating_hook(const safety_inputs_t *in, uint32_t *warning_ms, uint32_t *shutdown_ms, void *ctx)
{
    (void)ctx;
    if (isfinite(in->temp_rate_c_per_min) && in->temp_rate_c_per_min > 10.0f) {
        *warning_ms /= 2;
        *shutdown_ms /= 2;
    }
}

void app_main(void)
{
    const safety_config_t cfg = rb_safety_config_from_kconfig();
    if (!safety_config_valid(&cfg)) {
        ESP_LOGE(TAG, "menuconfig safety settings are inconsistent (shutdown must come after warning, off <= on)");
        return;
    }
    const uint32_t W = cfg.warning_timeout_ms;
    const uint32_t S = shutdown_after_leaving(&cfg);
    const uint32_t D = cfg.absence_debounce_ms;
    ESP_LOGI(TAG, "warning after %" PRIu32 " s unattended, shutdown %" PRIu32 " s after leaving, absence debounce %" PRIu32 " ms, cooking >= %.1f C",
             W / 1000, S / 1000, D, cfg.heat_on_temp_c);
    static sim_t s;

    ESP_LOGI(TAG, "--- leave, come back, leave again ---");
    start_monitoring(&s, &cfg);
    s.in.presence = RB_FALSE;
    run(&s, D + STEP_MS);
    expect(&s, SAFETY_UNATTENDED, "MONITORING -> UNATTENDED after the absence debounce");
    run(&s, W / 2);
    s.in.presence = RB_TRUE;
    run(&s, cfg.presence_return_debounce_ms + STEP_MS);
    expect(&s, SAFETY_MONITORING, "UNATTENDED -> MONITORING when the person returns");
    s.in.presence = RB_FALSE;
    run(&s, W - 2 * STEP_MS);
    expect(&s, SAFETY_UNATTENDED, "timer was reset: no WARNING before a full warning timeout");

    ESP_LOGI(TAG, "--- stay away until shutdown ---");
    start_monitoring(&s, &cfg);
    s.in.presence = RB_FALSE;
    run(&s, W - STEP_MS);
    expect(&s, SAFETY_UNATTENDED, "still UNATTENDED just before the warning timeout");
    run(&s, 2 * STEP_MS);
    expect(&s, SAFETY_WARNING, "UNATTENDED -> WARNING at the warning timeout (buzzer on)");
    run(&s, S - W - 2 * STEP_MS);
    expect(&s, SAFETY_WARNING, "still WARNING just before the shutdown timeout");
    run(&s, 2 * STEP_MS);
    expect(&s, SAFETY_SHUTDOWN, "WARNING -> SHUTDOWN at the shutdown timeout (relay activated)");

    ESP_LOGI(TAG, "--- temperature trend shortens the timers (hook) ---");
    safety_config_t hooked = cfg;
    hooked.timing_hook = fast_heating_hook;
    start_monitoring(&s, &hooked);
    s.in.temp_rate_c_per_min = 20.0f;
    s.in.presence = RB_FALSE;
    run(&s, W / 2 + STEP_MS);
    expect(&s, SAFETY_WARNING, "fast heating halves the warning timeout");

    if (s_fail == 0) {
        ESP_LOGI(TAG, "all %d behaviour checks PASSED", s_pass);
    } else {
        ESP_LOGE(TAG, "%d of %d behaviour checks FAILED", s_fail, s_pass + s_fail);
    }
}
