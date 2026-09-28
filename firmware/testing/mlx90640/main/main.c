/*
 * TODO section 3: MLX90640 integration and thermal feature extraction.
 *
 * Reads complete 32x24 frames, checks every one of the 768 temperatures,
 * extracts the features used by the safety logic and logs one summary line a
 * second. The raw frame can be dumped over serial for diagnostics (menuconfig
 * RB_THERMAL_DIAG_DUMP_EVERY); it is never sent over ESP-NOW.
 */
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mlx90640.h"
#include "rb_config.h"
#include "rb_node_sensors.h"
#include "thermal_features.h"

static const char *TAG = "THERMAL";

#define SUMMARY_PERIOD_MS 1000
#define RETRY_INIT_MS 5000

static float s_frame[MLX90640_PIXELS];

static void dump_frame(void)
{
    /* One line: FRAME,<768 values>. Row-major, 0.1 degC resolution. */
    printf("FRAME");
    for (int i = 0; i < MLX90640_PIXELS; i++) {
        printf(",%.1f", s_frame[i]);
    }
    printf("\n");
}

void app_main(void)
{
    while (rb_node_thermal_start() != ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(RETRY_INIT_MS));
    }

    const thermal_features_config_t cfg = rb_node_thermal_features_config();
    thermal_rate_tracker_t tracker;
    thermal_rate_reset(&tracker);
    uint32_t frames = 0, read_errors = 0, invalid_frames = 0;
    int64_t next_summary = 0;

    for (;;) {
        float ta = NAN;
        esp_err_t err = mlx90640_read_frame(s_frame, &ta, CONFIG_RB_MLX_FRAME_TIMEOUT_MS);
        const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (err != ESP_OK) {
            read_errors++;
            ESP_LOGE(TAG, "frame read failed: %s (driver err %d)", esp_err_to_name(err), mlx90640_last_error());
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        frames++;

        thermal_reading_t r;
        thermal_frame_info_t info;
        if (!thermal_features_compute(s_frame, now_ms, &cfg, &tracker, &r, &info)) {
            invalid_frames++;
            ESP_LOGW(TAG, "invalid frame: %u/768 pixels out of range", info.invalid_pixels);
        }
        if (CONFIG_RB_THERMAL_DIAG_DUMP_EVERY > 0 && frames % CONFIG_RB_THERMAL_DIAG_DUMP_EVERY == 0) {
            dump_frame();
        }
        if (esp_timer_get_time() >= next_summary && r.valid) {
            next_summary = esp_timer_get_time() + SUMMARY_PERIOD_MS * 1000LL;
            ESP_LOGI(TAG, "max %.1f min %.1f mean %.1f hot-region %.1f @(%u,%u) | >%.0fC: %u px | rate %.2f C/min | Ta %.1f | frames %" PRIu32 " err %" PRIu32 " invalid %" PRIu32,
                     r.max_temp_c, r.min_temp_c, r.mean_temp_c, r.hot_region_temp_c, info.hottest_col,
                     info.hottest_row, cfg.hot_pixel_threshold_c, r.pixels_above_threshold, r.temp_rate_c_per_min,
                     ta, frames, read_errors, invalid_frames);
        }
    }
}
