#include "thermal_features.h"

#include <math.h>
#include <string.h>

void thermal_rate_reset(thermal_rate_tracker_t *tracker)
{
    memset(tracker, 0, sizeof(*tracker));
}

static void rate_push(thermal_rate_tracker_t *tr, uint32_t now_ms, float temp_c, uint32_t window_ms)
{
    /* Spread samples over the window so the buffer covers it. */
    const uint32_t spacing = window_ms / THERMAL_RATE_SAMPLES;
    if (tr->count > 0) {
        const uint8_t last = (uint8_t)((tr->head + THERMAL_RATE_SAMPLES - 1) % THERMAL_RATE_SAMPLES);
        if (now_ms - tr->t_ms[last] < spacing) {
            return;
        }
    }
    tr->t_ms[tr->head] = now_ms;
    tr->temp_c[tr->head] = temp_c;
    tr->head = (uint8_t)((tr->head + 1) % THERMAL_RATE_SAMPLES);
    if (tr->count < THERMAL_RATE_SAMPLES) {
        tr->count++;
    }
}

static float rate_estimate(const thermal_rate_tracker_t *tr, uint32_t now_ms, float temp_now, uint32_t window_ms)
{
    if (tr->count == 0) {
        return NAN;
    }
    /* Oldest sample still inside the window. */
    for (uint8_t i = 0; i < tr->count; i++) {
        const uint8_t idx = (uint8_t)((tr->head + THERMAL_RATE_SAMPLES - tr->count + i) % THERMAL_RATE_SAMPLES);
        const uint32_t age = now_ms - tr->t_ms[idx];
        if (age <= window_ms) {
            if (age < window_ms / 4 || age == 0) {
                return NAN;
            }
            return (temp_now - tr->temp_c[idx]) * 60000.0f / (float)age;
        }
    }
    return NAN;
}

bool thermal_features_compute(const float frame[THERMAL_PIXELS], uint32_t now_ms,
                              const thermal_features_config_t *cfg, thermal_rate_tracker_t *tracker,
                              thermal_reading_t *out, thermal_frame_info_t *info)
{
    memset(out, 0, sizeof(*out));
    out->timestamp_ms = now_ms;
    out->temp_rate_c_per_min = NAN;

    uint16_t invalid = 0;
    uint16_t above = 0;
    uint16_t counted = 0;
    float sum = 0.0f;
    float max_c = -INFINITY;
    float min_c = INFINITY;
    int hottest = -1;

    for (int i = 0; i < THERMAL_PIXELS; i++) {
        const float t = frame[i];
        if (!isfinite(t) || t < cfg->valid_min_c || t > cfg->valid_max_c) {
            invalid++;
            continue;
        }
        counted++;
        sum += t;
        if (t > max_c) {
            max_c = t;
            hottest = i;
        }
        if (t < min_c) {
            min_c = t;
        }
        if (t >= cfg->hot_pixel_threshold_c) {
            above++;
        }
    }

    const int hot_col = hottest >= 0 ? hottest % THERMAL_COLS : 0;
    const int hot_row = hottest >= 0 ? hottest / THERMAL_COLS : 0;
    if (info != NULL) {
        info->invalid_pixels = invalid;
        info->hottest_col = (uint8_t)hot_col;
        info->hottest_row = (uint8_t)hot_row;
    }
    if (counted == 0 || invalid > cfg->max_invalid_pixels) {
        return false;
    }

    /* Mean of the valid pixels in the square around the hottest pixel. */
    const int r = cfg->hot_region_radius;
    float region_sum = 0.0f;
    int region_n = 0;
    for (int row = hot_row - r; row <= hot_row + r; row++) {
        for (int col = hot_col - r; col <= hot_col + r; col++) {
            if (row < 0 || row >= THERMAL_ROWS || col < 0 || col >= THERMAL_COLS) {
                continue;
            }
            const float t = frame[row * THERMAL_COLS + col];
            if (isfinite(t) && t >= cfg->valid_min_c && t <= cfg->valid_max_c) {
                region_sum += t;
                region_n++;
            }
        }
    }

    out->valid = true;
    out->max_temp_c = max_c;
    out->min_temp_c = min_c;
    out->mean_temp_c = sum / (float)counted;
    out->hot_region_temp_c = region_sum / (float)region_n;
    out->pixels_above_threshold = above;
    out->temp_rate_c_per_min = rate_estimate(tracker, now_ms, out->hot_region_temp_c, cfg->rate_window_ms);
    rate_push(tracker, now_ms, out->hot_region_temp_c, cfg->rate_window_ms);
    return true;
}
