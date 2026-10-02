#include "node_thermal.h"

#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mlx90640.h"
#include "rb_config.h"
#include "rb_log.h"
#include "rb_node_sensors.h"
#include "thermal_features.h"
#include "thermal_frame.h"

static const char *TAG = "THERMAL";

#define THERMAL_TASK_STACK 4096
#define THERMAL_TASK_PRIO 5
#define INIT_RETRY_MS 5000

static float s_frame[MLX90640_PIXELS];
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static thermal_reading_t s_latest;
static bool s_have_frame;
static uint32_t s_frame_ms;

#if CONFIG_RB_THERMAL_HEATMAP
/*
 * Heat-map picture: the thermal task encodes into s_heatmap_work, then copies
 * it to s_heatmap (under s_lock) for the ESP-NOW link task. 2 x ~780 bytes.
 */
static thermal_frame_t s_heatmap_work;
static thermal_frame_t s_heatmap;
static uint32_t s_heatmap_number; /* 0 = no picture yet */
static uint32_t s_heatmap_next_ms;

static void update_heatmap(const thermal_features_config_t *cfg, uint32_t now)
{
    if ((int32_t)(now - s_heatmap_next_ms) < 0) {
        return;
    }
    s_heatmap_next_ms = now + CONFIG_RB_THERMAL_HEATMAP_PERIOD_MS;
    thermal_frame_encode(s_frame, cfg->valid_min_c, cfg->valid_max_c, now, &s_heatmap_work);
    portENTER_CRITICAL(&s_lock);
    s_heatmap = s_heatmap_work;
    s_heatmap_number++;
    portEXIT_CRITICAL(&s_lock);
}
#endif

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void thermal_task(void *arg)
{
    while (rb_node_thermal_start() != ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(INIT_RETRY_MS));
    }
    const thermal_features_config_t cfg = rb_node_thermal_features_config();
    thermal_rate_tracker_t tracker;
    thermal_rate_reset(&tracker);
    uint32_t failures = 0;

    for (;;) {
        esp_err_t err = mlx90640_read_frame(s_frame, NULL, CONFIG_RB_MLX_FRAME_TIMEOUT_MS);
        if (err != ESP_OK) {
            failures++;
            RB_LOG_EVERY_MS(5000, ESP_LOGE, TAG, "frame read failed (%s), %lu so far", esp_err_to_name(err),
                            (unsigned long)failures);
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        thermal_reading_t r;
        const uint32_t now = now_ms();
        thermal_features_compute(s_frame, now, &cfg, &tracker, &r, NULL);
#if CONFIG_RB_THERMAL_HEATMAP
        update_heatmap(&cfg, now);
#endif
        portENTER_CRITICAL(&s_lock);
        s_latest = r;
        s_have_frame = true;
        s_frame_ms = r.timestamp_ms;
        portEXIT_CRITICAL(&s_lock);
    }
}

esp_err_t node_thermal_start(void)
{
    return xTaskCreate(thermal_task, "thermal", THERMAL_TASK_STACK, NULL, THERMAL_TASK_PRIO, NULL) == pdPASS
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

void node_thermal_get(thermal_reading_t *out, bool *no_data)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_latest;
    const bool have = s_have_frame;
    const uint32_t at = s_frame_ms;
    portEXIT_CRITICAL(&s_lock);

    const bool stale = !have || (now_ms() - at) > CONFIG_RB_MLX_STALE_TIMEOUT_MS;
    if (stale) {
        memset(out, 0, sizeof(*out));
        out->temp_rate_c_per_min = NAN;
        out->timestamp_ms = now_ms();
    }
    *no_data = stale;
}

bool node_thermal_get_heatmap(thermal_frame_t *out, uint32_t *number)
{
#if CONFIG_RB_THERMAL_HEATMAP
    portENTER_CRITICAL(&s_lock);
    const bool fresh = s_heatmap_number != *number && s_heatmap_number != 0;
    if (fresh) {
        *out = s_heatmap;
        *number = s_heatmap_number;
    }
    portEXIT_CRITICAL(&s_lock);
    return fresh;
#else
    return false;
#endif
}
