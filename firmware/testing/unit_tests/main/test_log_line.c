/* The controller's log lines for the dashboard: parsing what ESP-IDF prints. */
#include <string.h>
#include "rb_log_capture.h"
#include "unity.h"

static rb_log_line_t parse(const char *text)
{
    rb_log_line_t line;
    TEST_ASSERT_TRUE_MESSAGE(rb_log_line_parse(text, strlen(text), 77, &line), text);
    return line;
}

static void test_plain_line(void)
{
    const rb_log_line_t l = parse("W (12835) SAFETY: MONITORING -> UNATTENDED (person absent)\n");
    TEST_ASSERT_EQUAL_CHAR('W', l.level);
    TEST_ASSERT_EQUAL_STRING("SAFETY", l.tag);
    TEST_ASSERT_EQUAL_STRING("MONITORING -> UNATTENDED (person absent)", l.msg);
    TEST_ASSERT_EQUAL_UINT32(77, l.uptime_ms);
}

static void test_colours_clock_time_and_wifi(void)
{
    rb_log_line_t l = parse("\033[0;31mE (2034) THERMAL: frame read failed (ESP_ERR_TIMEOUT)\033[0m\r\n");
    TEST_ASSERT_EQUAL_CHAR('E', l.level);
    TEST_ASSERT_EQUAL_STRING("THERMAL", l.tag);
    TEST_ASSERT_EQUAL_STRING("frame read failed (ESP_ERR_TIMEOUT)", l.msg);

    l = parse("I (12:01:02.345) MQTT: broker connected");
    TEST_ASSERT_EQUAL_STRING("MQTT", l.tag);
    TEST_ASSERT_EQUAL_STRING("broker connected", l.msg);

    l = parse("I (99) wifi:mode : sta (aa:bb)");
    TEST_ASSERT_EQUAL_STRING("wifi", l.tag);
    TEST_ASSERT_EQUAL_STRING("mode : sta (aa:bb)", l.msg);
}

static void test_other_shapes_kept_whole(void)
{
    rb_log_line_t l = parse("free heap 123: ok");
    TEST_ASSERT_EQUAL_CHAR('I', l.level);
    TEST_ASSERT_EQUAL_STRING("-", l.tag);
    TEST_ASSERT_EQUAL_STRING("free heap 123: ok", l.msg);

    l = parse("W (5) a tag with spaces: x");
    TEST_ASSERT_EQUAL_STRING("-", l.tag);
    TEST_ASSERT_EQUAL_STRING("W (5) a tag with spaces: x", l.msg);

    rb_log_line_t empty;
    TEST_ASSERT_FALSE(rb_log_line_parse("\033[0m\n", 5, 0, &empty));
}

static void test_unprintable_and_long(void)
{
    rb_log_line_t l = parse("D (1) NET: caf\xc3\xa9\tok");
    TEST_ASSERT_EQUAL_STRING("caf?? ok", l.msg);

    char text[400] = "I (1) SENSOR: ";
    memset(text + strlen(text), 'x', 300);
    l = parse(text);
    TEST_ASSERT_EQUAL(RB_LOG_LINE_MSG_LEN - 1, strlen(l.msg));
}

void run_log_line_tests(void)
{
    RUN_TEST(test_plain_line);
    RUN_TEST(test_colours_clock_time_and_wifi);
    RUN_TEST(test_other_shapes_kept_whole);
    RUN_TEST(test_unprintable_and_long);
}
