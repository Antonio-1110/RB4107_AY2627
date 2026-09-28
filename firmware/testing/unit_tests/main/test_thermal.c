/* Thermal feature extraction (section 3.1). */
#include <math.h>
#include "thermal_features.h"
#include "unity.h"

static const thermal_features_config_t CFG = {
    .hot_pixel_threshold_c = 50.0f, .hot_region_radius = 1, .rate_window_ms = 30000,
    .valid_min_c = -40.0f, .valid_max_c = 300.0f, .max_invalid_pixels = 8,
};

static float frame[THERMAL_PIXELS];

static void fill(float t)
{
    for (int i = 0; i < THERMAL_PIXELS; i++) {
        frame[i] = t;
    }
}

static void test_features(void)
{
    fill(25.0f);
    frame[5 * 32 + 10] = 120.0f;
    frame[5 * 32 + 11] = 60.0f;
    thermal_rate_tracker_t tr;
    thermal_rate_reset(&tr);
    thermal_reading_t r;
    thermal_frame_info_t info;
    TEST_ASSERT_TRUE(thermal_features_compute(frame, 0, &CFG, &tr, &r, &info));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 120.0f, r.max_temp_c);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 25.0f, r.min_temp_c);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, (120.0f + 60.0f + 7 * 25.0f) / 9.0f, r.hot_region_temp_c);
    TEST_ASSERT_EQUAL_UINT16(2, r.pixels_above_threshold);
    TEST_ASSERT_EQUAL_UINT8(10, info.hottest_col);
    TEST_ASSERT_EQUAL_UINT8(5, info.hottest_row);
    TEST_ASSERT_TRUE(isnan(r.temp_rate_c_per_min)); /* no history yet */
}

static void test_rate_of_change(void)
{
    thermal_rate_tracker_t tr;
    thermal_rate_reset(&tr);
    thermal_reading_t r;
    for (uint32_t s = 0; s <= 40; s++) { /* +3 degC per minute across the whole frame */
        fill(25.0f + 3.0f * s / 60.0f);
        thermal_features_compute(frame, s * 1000, &CFG, &tr, &r, NULL);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 3.0f, r.temp_rate_c_per_min);
}

static void test_invalid_frames(void)
{
    thermal_rate_tracker_t tr;
    thermal_rate_reset(&tr);
    thermal_reading_t r;
    thermal_frame_info_t info;
    fill(25.0f);
    for (int i = 0; i < 9; i++) {
        frame[i] = NAN;
    }
    TEST_ASSERT_FALSE(thermal_features_compute(frame, 0, &CFG, &tr, &r, &info));
    TEST_ASSERT_EQUAL_UINT16(9, info.invalid_pixels);
    fill(25.0f);
    for (int i = 0; i < 8; i++) {
        frame[i] = 999.0f; /* out of range but within tolerance */
    }
    TEST_ASSERT_TRUE(thermal_features_compute(frame, 0, &CFG, &tr, &r, &info));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 25.0f, r.max_temp_c);
}

void run_thermal_tests(void)
{
    RUN_TEST(test_features);
    RUN_TEST(test_rate_of_change);
    RUN_TEST(test_invalid_frames);
}
