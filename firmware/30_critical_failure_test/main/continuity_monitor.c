#include "continuity_monitor.h"

#include <inttypes.h>
#include "esp_log.h"
#include "fault_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rb_config.h"
#include "rb_controller.h"
#include "rb_mqtt.h"
#include "rb_time.h"

static const char *TAG = "CRITICAL";

#define SAMPLE_MS 200
#define REPORT_MS 5000
/* The safety task ticks every RB_CTRL_SAFETY_TICK_MS; allow generous slack. */
#define MAX_LOOP_GAP_MS (5 * CONFIG_RB_CTRL_SAFETY_TICK_MS)

typedef struct {
    bool active;
    uint32_t start_ms;
    uint32_t start_loops;
    uint32_t start_transitions;
    uint32_t max_gap_ms;
    bool fault_reported;
    safety_state_t state_before_reconnect;
} outage_t;

static void monitor_task(void *arg)
{
    outage_t o = {0};
    uint32_t last_report = 0;
    rb_snapshot_t prev = {0};

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
        const uint32_t now = rb_time_mono_ms();
        rb_snapshot_t s;
        rb_controller_get_snapshot(&s);
        const bool mqtt_up = rb_mqtt_state() == RB_MQTT_CONNECTED;
        const uint32_t gap = now - s.last_loop_ms;

        if (!mqtt_up && !o.active && prev.loop_count != 0) {
            o = (outage_t){.active = true, .start_ms = now, .start_loops = s.loop_count,
                           .start_transitions = s.transitions};
            ESP_LOGW(TAG, "MQTT DOWN: monitoring the safety path (state %s)", safety_state_name(s.state));
        }
        if (o.active) {
            o.max_gap_ms = gap > o.max_gap_ms ? gap : o.max_gap_ms;
            o.fault_reported |= fault_is_active(FAULT_MQTT_DOWN);
            o.state_before_reconnect = prev.state;
            if (rb_time_elapsed(now, last_report, REPORT_MS)) {
                last_report = now;
                const uint32_t secs = (now - o.start_ms) / 1000;
                ESP_LOGW(TAG, "MQTT down %" PRIu32 " s: safety loop %s (last iteration %" PRIu32 " ms ago, %" PRIu32
                              " loops so far), state %s, node %s, buzzer %s, relay %s",
                         secs, gap <= MAX_LOOP_GAP_MS ? "ALIVE" : "STALLED", gap, s.loop_count - o.start_loops,
                         safety_state_name(s.state), node_link_state_name(s.node.link),
                         safety_buzzer_name(s.outputs.buzzer), s.outputs.shutdown ? "ACTIVE" : "released");
            }
            if (mqtt_up) {
                /* Reconnected: did the state survive, and did the loop run the whole time? */
                const bool loop_ok = o.max_gap_ms <= MAX_LOOP_GAP_MS && s.loop_count > o.start_loops;
                const bool state_kept = s.state == o.state_before_reconnect;
                const bool pass = loop_ok && state_kept && o.fault_reported;
                const uint32_t secs = (now - o.start_ms) / 1000;
                const char *verdict = pass ? "PASS" : "FAIL";
                ESP_LOGW(TAG, "outage %" PRIu32 " s: max safety-loop gap %" PRIu32 " ms, %" PRIu32 " loops, %" PRIu32
                              " transitions during outage, mqtt fault %s, state across reconnect %s -> %s -> %s",
                         secs, o.max_gap_ms, s.loop_count - o.start_loops, s.transitions - o.start_transitions,
                         o.fault_reported ? "reported" : "NOT reported", safety_state_name(o.state_before_reconnect),
                         safety_state_name(s.state), verdict);
                o.active = false;
            }
        }
        prev = s;
    }
}

esp_err_t continuity_monitor_start(void)
{
    return xTaskCreate(monitor_task, "continuity", 4096, NULL, 1, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
