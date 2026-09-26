/* MQTT JSON and topics (sections 20-21). Full schema validation runs on the host (tools/diagnostics). */
#include <math.h>
#include <string.h>
#include "rb_json.h"
#include "rb_json_writer.h"
#include "rb_topics.h"
#include "unity.h"

static char buf[1024];

static const rb_json_node_t NODES[] = {
    {"node_01", "presence", "ONLINE", true, true},
    {"node_02", "presence", "OFFLINE", false, true},   /* invalid: "detected" must be null */
    {"node_03", "thermal", "ONLINE", true, false},     /* thermal: never has "detected" */
};

static rb_telemetry_t sample(void)
{
    static const rb_json_fault_t faults[] = {{"mqtt_disconnected", "TELEMETRY"}};
    return (rb_telemetry_t){
        .hdr = {"controller_01", NULL, 1000, 1},
        .presence_state = "PRESENT",
        .nodes = NODES,
        .node_count = 3,
        .sensor_node = "node_02",
        .node = {"presence", "OFFLINE", false, 0, 0, 0},
        .presence = {.valid = false, .detected = false, .distance_m = NAN},
        .thermal = {.valid = true, .max_c = 84.2f, .rate_c_per_min = NAN},
        .safety = {"UNATTENDED", 1, 2, "OFF", false, false, 3},
        .faults = faults,
        .fault_count = 1,
    };
}

static void test_unknown_values_are_null(void)
{
    const rb_telemetry_t t = sample();
    TEST_ASSERT_NOT_EQUAL(0, rb_json_telemetry(&t, buf, sizeof(buf)));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"timestamp\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"presence_state\":\"PRESENT\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "{\"sensor_node\":\"node_01\",\"role\":\"presence\",\"link\":\"ONLINE\","
                                     "\"valid\":true,\"detected\":true}"));
    /* An offline radar's last reading is not reported as a person (or as nobody). */
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"link\":\"OFFLINE\",\"valid\":false,\"detected\":null}"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"role\":\"thermal\",\"link\":\"ONLINE\",\"valid\":true,\"detected\":null}"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"rate_c_per_min\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"faults\":[\"mqtt_disconnected\"]"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"schema_version\":2"));

    TEST_ASSERT_NOT_EQUAL(0, rb_json_presence(&t, buf, sizeof(buf)));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"detected\":null")); /* invalid presence is not "false" */
    TEST_ASSERT_NOT_EQUAL(0, rb_json_node_status(&t, buf, sizeof(buf)));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"sensor_node\":\"node_02\",\"role\":\"presence\",\"link\":\"OFFLINE\","
                                     "\"valid\":false"));
}

static void test_overflow_returns_zero(void)
{
    const rb_telemetry_t t = sample();
    char small[64];
    TEST_ASSERT_EQUAL(0, rb_json_telemetry(&t, small, sizeof(small)));
}

static void test_string_escaping(void)
{
    rb_json_writer_t w;
    rb_json_init(&w, buf, sizeof(buf));
    rb_json_obj_begin(&w);
    rb_json_key(&w, "r");
    rb_json_str(&w, "a\"b\\c\n\x01");
    rb_json_obj_end(&w);
    TEST_ASSERT_NOT_EQUAL(0, rb_json_finish(&w));
    TEST_ASSERT_EQUAL_STRING("{\"r\":\"a\\\"b\\\\c\\n\\u0001\"}", buf);
}

static void test_unbalanced_is_rejected(void)
{
    rb_json_writer_t w;
    rb_json_init(&w, buf, sizeof(buf));
    rb_json_obj_begin(&w);
    TEST_ASSERT_EQUAL(0, rb_json_finish(&w));
}

static void test_topics(void)
{
    char topic[96];
    TEST_ASSERT_NOT_EQUAL(0, rb_topic_build(RB_TOPIC_SENSOR_THERMAL, "rb4107", 1, topic, sizeof(topic)));
    TEST_ASSERT_EQUAL_STRING("rb4107/sensors/node_01/thermal", topic);
    rb_topic_build(RB_TOPIC_EVENT_SHUTDOWN, "lab", 0, topic, sizeof(topic));
    TEST_ASSERT_EQUAL_STRING("lab/events/shutdown", topic);
    TEST_ASSERT_EQUAL(1, rb_topic_info(RB_TOPIC_EVENT_SHUTDOWN)->qos);
    TEST_ASSERT_EQUAL(0, rb_topic_info(RB_TOPIC_CONTROLLER_HEARTBEAT)->qos);
    TEST_ASSERT_TRUE(rb_topic_info(RB_TOPIC_CONTROLLER_STATUS)->retain);
    TEST_ASSERT_EQUAL(0, rb_topic_build(RB_TOPIC_CONTROLLER_STATE, "rb4107", 0, topic, 8));
}

void run_json_tests(void)
{
    RUN_TEST(test_unknown_values_are_null);
    RUN_TEST(test_overflow_returns_zero);
    RUN_TEST(test_string_escaping);
    RUN_TEST(test_unbalanced_is_rejected);
    RUN_TEST(test_topics);
}
