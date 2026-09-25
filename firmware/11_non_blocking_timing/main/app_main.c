/*
 * TODO section 11: non-blocking timing.
 *
 * The safety task never sleeps for a timeout. It waits on its input queue
 * for at most one tick, handles every sensor update as it arrives, and steps
 * the state machine with the esp_timer monotonic clock. Timers are
 * comparisons against state-entry timestamps:
 *
 *     if (state == UNATTENDED && now - unattended_start >= warning_timeout) -> WARNING
 *
 * A simulated sensor task sends 5 updates/s the whole time. The log shows
 * the safety task handling all of them while the (shortened) warning and
 * shutdown timers run in real time.
 */
#include <inttypes.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "rb_time.h"
#include "safety.h"

static const char *TAG = "SAFETY";

#define SAFETY_TICK_MS 100
#define SENSOR_PERIOD_MS 200
#define CYCLE_MS 30000u

typedef struct {
    rb_tristate_t presence;
    float hot_region_temp_c;
    uint32_t sequence;
} sim_update_t;

static QueueHandle_t s_updates;

/* Stand-in for the ESP-NOW receive path: a new sensor update every 200 ms. */
static void sensor_sim_task(void *arg)
{
    uint32_t seq = 0;
    const uint32_t start = rb_time_mono_ms();
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        const uint32_t t = (rb_time_mono_ms() - start) % CYCLE_MS;
        /* Script: cook present for 3 s, then away for the rest of the cycle. */
        sim_update_t u = {
            .presence = t < 3000 ? RB_TRUE : RB_FALSE,
            .hot_region_temp_c = 120.0f,
            .sequence = ++seq,
        };
        xQueueSend(s_updates, &u, 0);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(SENSOR_PERIOD_MS));
    }
}

static void on_transition(safety_state_t from, safety_state_t to, const char *reason, uint32_t now_ms, void *ctx)
{
    ESP_LOGW(TAG, "%s -> %s (%s) at %" PRIu32 " ms", safety_state_name(from), safety_state_name(to), reason, now_ms);
}

static void safety_task(void *arg)
{
    const safety_config_t cfg = {
        .self_test_timeout_ms = 5000,
        .heat_on_temp_c = 50.0f,
        .heat_off_temp_c = 40.0f,
        .absence_debounce_ms = 1000,
        .warning_timeout_ms = 5000,  /* shortened for the demo */
        .shutdown_timeout_ms = 10000,
        .shutdown_timing = SAFETY_SHUTDOWN_AFTER_UNATTENDED_START,
        .fault_shutdown_timeout_ms = 10000,
    };
    static safety_sm_t sm;
    safety_init(&sm, &cfg, on_transition, NULL, rb_time_mono_ms());
    safety_inputs_t in = {.presence = RB_UNKNOWN, .self_test = SAFETY_SELFTEST_PASS};
    uint32_t processed = 0, last_report = 0;

    for (;;) {
        sim_update_t u;
        /* Wait for data, but never longer than one tick: timers are checked on every pass. */
        if (xQueueReceive(s_updates, &u, pdMS_TO_TICKS(SAFETY_TICK_MS)) == pdTRUE) {
            in.presence = u.presence;
            in.thermal_valid = true;
            in.hot_region_temp_c = u.hot_region_temp_c;
            processed++;
        }
        const uint32_t now = rb_time_mono_ms();
        /* The returning cook resets the shutdown so the demo repeats every cycle. */
        in.reset_request = sm.state == SAFETY_SHUTDOWN && in.presence == RB_TRUE;
        safety_step(&sm, &in, now);

        if (rb_time_elapsed(now, last_report, 1000)) {
            last_report = now;
            ESP_LOGI(TAG, "state %-10s for %5" PRIu32 " ms | unattended %5" PRIu32 " ms | sensor updates processed %" PRIu32,
                     safety_state_name(sm.state), now - sm.state_entered_ms, safety_unattended_ms(&sm, now), processed);
        }
    }
}

void app_main(void)
{
    s_updates = xQueueCreate(8, sizeof(sim_update_t));
    xTaskCreate(safety_task, "safety", 4096, NULL, 10, NULL);
    xTaskCreate(sensor_sim_task, "sensor_sim", 3072, NULL, 8, NULL);
}
