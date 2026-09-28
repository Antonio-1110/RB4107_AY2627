/* C4002 UART frame codec (section 2). */
#include <math.h>
#include "c4002_proto.h"
#include "unity.h"

static const uint8_t RESULT_MOTION[18] = {2, 0x10, 0, 1, 0, 0, 0, 3, 0, 0x2c, 1, 40, 0x96, 0, 0xf6, 0xff, 55, 2};

static int feed_all(c4002_parser_t *p, const uint8_t *buf, size_t len, c4002_frame_t *out, c4002_parse_status_t want)
{
    int n = 0;
    for (size_t i = 0; i < len; i++) {
        if (c4002_parser_feed(p, buf[i], out) == want) {
            n++;
        }
    }
    return n;
}

static void test_frame_roundtrip_and_decode(void)
{
    uint8_t f[C4002_MAX_FRAME];
    const size_t n = c4002_build_frame(C4002_FRAME_NOTIFICATION, C4002_NOTE_RESULT, RESULT_MOTION, 18, f, sizeof(f));
    TEST_ASSERT_EQUAL(32, n);
    c4002_parser_t p;
    c4002_parser_reset(&p);
    c4002_frame_t fr;
    TEST_ASSERT_EQUAL(1, feed_all(&p, f, n, &fr, C4002_PARSE_FRAME));
    c4002_result_t r;
    TEST_ASSERT_TRUE(c4002_decode_result(&fr, &r));
    TEST_ASSERT_EQUAL(C4002_TARGET_MOTION, r.target_state);
    TEST_ASSERT_EQUAL_UINT16(300, r.presence_distance_cm);
    TEST_ASSERT_EQUAL_UINT16(150, r.motion_distance_cm);
    TEST_ASSERT_EQUAL_INT16(-10, r.motion_speed_cm_s);
    presence_reading_t pr;
    c4002_result_to_presence(&r, 1, &pr);
    TEST_ASSERT_TRUE(pr.valid && pr.presence_detected && pr.moving_target && !pr.stationary_target);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.5f, pr.distance_m);
}

static void test_resync_after_noise(void)
{
    uint8_t f[C4002_MAX_FRAME];
    const size_t n = c4002_build_frame(C4002_FRAME_NOTIFICATION, C4002_NOTE_RESULT, RESULT_MOTION, 18, f, sizeof(f));
    const uint8_t noise[] = {0x00, 0xFA, 0xFA, 0xF5, 0x13, 0xFA};
    c4002_parser_t p;
    c4002_parser_reset(&p);
    c4002_frame_t fr;
    feed_all(&p, noise, sizeof(noise), &fr, C4002_PARSE_FRAME);
    TEST_ASSERT_EQUAL(1, feed_all(&p, f, n, &fr, C4002_PARSE_FRAME));
}

static void test_checksum_and_length_errors(void)
{
    uint8_t f[C4002_MAX_FRAME];
    const size_t n = c4002_build_frame(C4002_FRAME_NOTIFICATION, C4002_NOTE_RESULT, RESULT_MOTION, 18, f, sizeof(f));
    c4002_parser_t p;
    c4002_frame_t fr;
    f[20] ^= 0x01;
    c4002_parser_reset(&p);
    TEST_ASSERT_EQUAL(1, feed_all(&p, f, n, &fr, C4002_PARSE_ERR_CHECKSUM));
    const uint8_t huge[] = {0xFA, 0xF5, 0xAA, 0xA5, 0xFF, 0x7F};
    c4002_parser_reset(&p);
    TEST_ASSERT_EQUAL(1, feed_all(&p, huge, sizeof(huge), &fr, C4002_PARSE_ERR_LENGTH));
}

static void test_invalid_readings(void)
{
    c4002_result_t r = {.target_state = 7};
    presence_reading_t pr;
    c4002_result_to_presence(&r, 0, &pr);
    TEST_ASSERT_FALSE(pr.valid); /* unknown state */
    r = (c4002_result_t){.target_state = C4002_TARGET_PRESENCE, .presence_distance_cm = 5000};
    c4002_result_to_presence(&r, 0, &pr);
    TEST_ASSERT_FALSE(pr.valid); /* impossible distance */
    r = (c4002_result_t){.target_state = C4002_TARGET_NONE};
    c4002_result_to_presence(&r, 0, &pr);
    TEST_ASSERT_TRUE(pr.valid);
    TEST_ASSERT_FALSE(pr.presence_detected);
    TEST_ASSERT_TRUE(isnan(pr.distance_m));
}

void run_c4002_tests(void)
{
    RUN_TEST(test_frame_roundtrip_and_decode);
    RUN_TEST(test_resync_after_noise);
    RUN_TEST(test_checksum_and_length_errors);
    RUN_TEST(test_invalid_readings);
}
