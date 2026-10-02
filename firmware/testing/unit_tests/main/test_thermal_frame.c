/* Heat-map pictures: 1-byte encoding on the node and reassembly on the controller (display only). */
#include <math.h>
#include <string.h>
#include "thermal_frame.h"
#include "unity.h"

static float temps[RB_FRAME_PIXELS];

static void fill_scene(void)
{
    for (unsigned i = 0; i < RB_FRAME_PIXELS; i++) {
        temps[i] = 22.0f + (float)(i % RB_FRAME_COLS) * 0.05f; /* room temperature gradient */
    }
    temps[5 * RB_FRAME_COLS + 7] = 180.0f; /* a hot pan */
}

static void test_encode_round_trip(void)
{
    fill_scene();
    thermal_frame_t f;
    TEST_ASSERT_EQUAL_UINT16(RB_FRAME_PIXELS, thermal_frame_encode(temps, -40.0f, 300.0f, 999, &f));
    TEST_ASSERT_EQUAL_UINT32(999, f.timestamp_ms);
    /* 158 degC spread over 254 levels: a step of 0.63 degC, so every pixel is within half a step. */
    const float half_step = f.step_centi / 200.0f;
    for (unsigned i = 0; i < RB_FRAME_PIXELS; i++) {
        TEST_ASSERT_FLOAT_WITHIN(half_step + 0.01f, temps[i], thermal_frame_pixel_c(&f, i));
    }
    /* The coldest pixel is level 0; the hottest is near the top without reaching the "invalid" value. */
    TEST_ASSERT_EQUAL_UINT8(0, f.pixels[0]);
    TEST_ASSERT_TRUE(f.pixels[5 * RB_FRAME_COLS + 7] >= 250 && f.pixels[5 * RB_FRAME_COLS + 7] < RB_FRAME_PIXEL_INVALID);
}

static void test_narrow_scene_uses_finest_step(void)
{
    for (unsigned i = 0; i < RB_FRAME_PIXELS; i++) {
        temps[i] = 25.0f + (float)(i % 3);
    }
    thermal_frame_t f;
    thermal_frame_encode(temps, -40.0f, 300.0f, 0, &f);
    TEST_ASSERT_EQUAL_UINT16(THERMAL_FRAME_MIN_STEP_CENTI, f.step_centi);
    TEST_ASSERT_FLOAT_WITHIN(0.06f, 27.0f, thermal_frame_pixel_c(&f, 2));
}

static void test_invalid_pixels(void)
{
    fill_scene();
    temps[0] = NAN;
    temps[1] = 900.0f;  /* above the plausible range */
    temps[2] = -100.0f; /* below it */
    thermal_frame_t f;
    TEST_ASSERT_EQUAL_UINT16(RB_FRAME_PIXELS - 3, thermal_frame_encode(temps, -40.0f, 300.0f, 0, &f));
    for (unsigned i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_UINT8(RB_FRAME_PIXEL_INVALID, f.pixels[i]);
        TEST_ASSERT_TRUE(isnan(thermal_frame_pixel_c(&f, i)));
    }
    /* No valid pixel at all: every pixel invalid, still a usable (empty) picture. */
    for (unsigned i = 0; i < RB_FRAME_PIXELS; i++) {
        temps[i] = NAN;
    }
    TEST_ASSERT_EQUAL_UINT16(0, thermal_frame_encode(temps, -40.0f, 300.0f, 0, &f));
    TEST_ASSERT_EQUAL_UINT8(RB_FRAME_PIXEL_INVALID, f.pixels[RB_FRAME_PIXELS - 1]);
    TEST_ASSERT_NOT_EQUAL(0, f.step_centi);
}

static void test_full_range(void)
{
    for (unsigned i = 0; i < RB_FRAME_PIXELS; i++) {
        temps[i] = (i & 1) ? 300.0f : -40.0f; /* the sensor's whole range */
    }
    thermal_frame_t f;
    thermal_frame_encode(temps, -40.0f, 300.0f, 0, &f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -40.0f, thermal_frame_pixel_c(&f, 0));
    TEST_ASSERT_FLOAT_WITHIN(f.step_centi / 200.0f + 0.01f, 300.0f, thermal_frame_pixel_c(&f, 1));
}

static thermal_frame_t encoded(void)
{
    fill_scene();
    thermal_frame_t f;
    thermal_frame_encode(temps, -40.0f, 300.0f, 4242, &f);
    f.hot_threshold_centi = 5000;
    f.hot_region_radius = 2;
    return f;
}

static void test_assemble_in_any_order(void)
{
    const thermal_frame_t f = encoded();
    thermal_frame_assembler_t a;
    thermal_frame_assembler_init(&a);
    thermal_frame_t out;
    rb_thermal_frame_piece_t p;
    const uint8_t order[RB_FRAME_PIECES] = {2, 0, 3, 1};
    for (unsigned k = 0; k < RB_FRAME_PIECES; k++) {
        thermal_frame_piece(&f, order[k], &p);
        const thermal_frame_result_t res = thermal_frame_assemble(&a, 3, 10, &p, &out);
        TEST_ASSERT_EQUAL(k + 1 < RB_FRAME_PIECES ? THERMAL_FRAME_PIECE_STORED : THERMAL_FRAME_COMPLETE, res);
        if (k == 0) { /* a repeated piece is ignored */
            TEST_ASSERT_EQUAL(THERMAL_FRAME_DUPLICATE, thermal_frame_assemble(&a, 3, 10, &p, &out));
        }
    }
    TEST_ASSERT_EQUAL_UINT8_ARRAY(f.pixels, out.pixels, RB_FRAME_PIXELS);
    TEST_ASSERT_EQUAL_INT16(f.base_centi, out.base_centi);
    TEST_ASSERT_EQUAL_UINT16(f.step_centi, out.step_centi);
    TEST_ASSERT_EQUAL_UINT32(4242, out.timestamp_ms);
    TEST_ASSERT_EQUAL_INT16(5000, out.hot_threshold_centi);
    TEST_ASSERT_EQUAL_UINT8(2, out.hot_region_radius);
    TEST_ASSERT_EQUAL_UINT32(1, a.stats.complete);
}

static void test_lost_piece_drops_only_that_frame(void)
{
    const thermal_frame_t f = encoded();
    thermal_frame_assembler_t a;
    thermal_frame_assembler_init(&a);
    thermal_frame_t out;
    rb_thermal_frame_piece_t p;
    for (uint8_t n = 0; n < RB_FRAME_PIECES; n++) {
        if (n == 1) {
            continue; /* lost over the air */
        }
        thermal_frame_piece(&f, n, &p);
        TEST_ASSERT_EQUAL(THERMAL_FRAME_PIECE_STORED, thermal_frame_assemble(&a, 3, 20, &p, &out));
    }
    /* The next frame arrives complete: it is delivered and the broken one is counted. */
    for (uint8_t n = 0; n < RB_FRAME_PIECES; n++) {
        thermal_frame_piece(&f, n, &p);
        const thermal_frame_result_t res = thermal_frame_assemble(&a, 3, 21, &p, &out);
        TEST_ASSERT_EQUAL(n + 1 < RB_FRAME_PIECES ? THERMAL_FRAME_PIECE_STORED : THERMAL_FRAME_COMPLETE, res);
    }
    TEST_ASSERT_EQUAL_UINT32(1, a.stats.complete);
    TEST_ASSERT_EQUAL_UINT32(1, a.stats.incomplete);
}

static void test_pieces_of_different_frames_never_mix(void)
{
    thermal_frame_t f = encoded();
    thermal_frame_assembler_t a;
    thermal_frame_assembler_init(&a);
    thermal_frame_t out;
    rb_thermal_frame_piece_t p;
    thermal_frame_piece(&f, 0, &p);
    thermal_frame_assemble(&a, 3, 30, &p, &out);
    /* Same frame number but a different scale (e.g. the node rebooted): the frame is discarded. */
    f.step_centi++;
    thermal_frame_piece(&f, 1, &p);
    TEST_ASSERT_EQUAL(THERMAL_FRAME_MISMATCH, thermal_frame_assemble(&a, 3, 30, &p, &out));
    TEST_ASSERT_EQUAL_UINT32(1, a.stats.mismatched);
    /* A piece from another node starts over instead of completing node 3's frame. */
    thermal_frame_piece(&f, 0, &p);
    thermal_frame_assemble(&a, 3, 31, &p, &out);
    for (uint8_t n = 1; n < RB_FRAME_PIECES; n++) {
        thermal_frame_piece(&f, n, &p);
        TEST_ASSERT_NOT_EQUAL(THERMAL_FRAME_COMPLETE, thermal_frame_assemble(&a, 9, 31, &p, &out));
    }
    TEST_ASSERT_EQUAL_UINT32(0, a.stats.complete);
}

void run_thermal_frame_tests(void)
{
    RUN_TEST(test_encode_round_trip);
    RUN_TEST(test_narrow_scene_uses_finest_step);
    RUN_TEST(test_invalid_pixels);
    RUN_TEST(test_full_range);
    RUN_TEST(test_assemble_in_any_order);
    RUN_TEST(test_lost_piece_drops_only_that_frame);
    RUN_TEST(test_pieces_of_different_frames_never_mix);
}
