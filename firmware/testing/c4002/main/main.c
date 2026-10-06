/*
 * TODO section 2: C4002 integration on the ESP32-C6.
 *
 * Brings up the C4002 over UART, pushes the menuconfig detection settings and
 * prints presence readings. It logs on every change plus a periodic summary
 * with the raw fields (energy, gate mask, hold countdown), which is what you
 * need to look into the false static-presence detections.
 */
#include <inttypes.h>
#include <math.h>
#include "c4002.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rb_config.h"
#include "rb_node_sensors.h"

/* The log tag is the current state, so each line shows it at a glance. */
static const char *s_tag = "NOTHING RECEIVED";

#define POLL_PERIOD_MS 100
#define SUMMARY_PERIOD_MS 5000

static const char *describe(const presence_reading_t *r)
{
    c4002_result_t raw;
    uint32_t age_ms;
    if (!c4002_get_raw(&raw, &age_ms)) {
        return "NOTHING RECEIVED";
    }
    if (!r->valid) {
        return age_ms > CONFIG_RB_C4002_STALE_TIMEOUT_MS ? "NO REPORT" : "INVALID";
    }
    if (r->moving_target) {
        return "MOVING";
    }
    if (r->stationary_target) {
        return "STATIONARY";
    }
    return "NO TARGET";
}

static void log_summary(void)
{
    c4002_result_t raw;
    uint32_t age_ms = 0;
    c4002_stats_t st;
    c4002_get_stats(&st);
    if (c4002_get_raw(&raw, &age_ms)) {
        ESP_LOGI(s_tag, "raw: state=%u age=%" PRIu32 "ms light=%.1flux | static: %ucm energy=%u gates=0x%05" PRIx32
                 " hold=%us | motion: %ucm energy=%u speed=%dcm/s dir=%u | OUT=%d",
                 raw.target_state, age_ms, raw.light_dlux / 10.0f, raw.presence_distance_cm, raw.presence_energy,
                 raw.presence_gate_mask, raw.presence_countdown_s, raw.motion_distance_cm, raw.motion_energy,
                 raw.motion_speed_cm_s, raw.motion_direction, c4002_get_out_level());
    } else {
        ESP_LOGW(s_tag, "no report received from the C4002 yet");
    }
    ESP_LOGI(s_tag, "stats: frames=%" PRIu32 " results=%" PRIu32 " checksum_err=%" PRIu32 " length_err=%" PRIu32
             " invalid=%" PRIu32 " cmd_timeout=%" PRIu32 " cmd_err=%" PRIu32,
             st.frames_ok, st.results, st.checksum_errors, st.length_errors, st.invalid_results,
             st.command_timeouts, st.command_errors);
}

void app_main(void)
{
    if (rb_node_c4002_start() != ESP_OK) {
        ESP_LOGE(s_tag, "C4002 setup failed; will keep listening for reports anyway");
    }

    const char *last = "";
    int64_t next_summary = 0;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        presence_reading_t r;
        c4002_get_reading(&r);
        const char *now = describe(&r);
        s_tag = now;
        if (now != last) {
            if (isnan(r.distance_m)) {
                ESP_LOGI(s_tag, "%s -> %s", last, now);
            } else {
                ESP_LOGI(s_tag, "%s -> %s at %.2f m", last, now, r.distance_m);
            }
            last = now;
        }
        if (esp_timer_get_time() >= next_summary) {
            next_summary = esp_timer_get_time() + SUMMARY_PERIOD_MS * 1000LL;
            log_summary();
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(POLL_PERIOD_MS));
    }
}
