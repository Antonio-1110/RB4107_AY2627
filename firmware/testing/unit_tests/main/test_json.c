/* MQTT JSON and topics (sections 20-21). Full schema validation runs on the host (tools/diagnostics). */
#include <math.h>
#include <string.h>
#include "rb_json.h"
#include "rb_json_writer.h"
#include "rb_params.h"
#include "rb_topics.h"
#include "unity.h"

static char buf[4096]; /* PAYLOAD_LEN in rb_telemetry.c */

static const rb_json_node_t NODES[] = {
    {"node_01", "presence", "ONLINE", true, true, NULL, 0, 0, 0},
    {"node_02", "presence", "OFFLINE", false, true, NULL, 0, 0, 0},   /* invalid: "detected" must be null */
    {"node_03", "thermal", "ONLINE", true, false, NULL, 0, 0, 0},     /* thermal: never has "detected" */
};

static rb_telemetry_t sample(void)
{
    static const rb_json_fault_t faults[] = {{"mqtt_disconnected", "TELEMETRY"}};
    return (rb_telemetry_t){
        .hdr = {.controller_id = "controller_01", .boot_id = "3f9a01c2", .timestamp = NULL, .uptime_ms = 1000,
                .sequence = 1},
        .presence_state = "PRESENT",
        .nearby = "PRESENT",
        .nodes = NODES,
        .node_count = 3,
        .sensor_node = "node_02",
        .node = {"presence", "OFFLINE", false, 0, 0, 0},
        .presence = {.valid = false, .detected = false, .distance_m = NAN},
        .thermal = {.valid = true, .max_c = 84.2f, .rate_c_per_min = NAN},
        .safety = {.state = "SHUTDOWN",
                   .state_ms = 1,
                   .unattended_ms = 2,
                   .buzzer = "SHUTDOWN",
                   .shutdown = true,
                   .reset_required = true,
                   .warning_after_ms = 60000,
                   .shutdown_after_ms = 90000,
                   .shutdown_counts_from = "UNATTENDED",
                   .absence_ms = 2000,
                   .return_ms = 3000,
                   .return_gap_ms = 1000,
                   .cooking_on_c = 55.5f,
                   .cooking_off_c = 45.5f,
                   .cooking_remote = true,
                   .cooking_active = true,
                   .cooking_phase = "cooling",
                   .trend_rate_c_per_min = -12.34f,
                   .settle_c = NAN,
                   .hold = "cooling",
                   .tier = "critical",
                   .effective_warning_ms = 30000,
                   .effective_shutdown_ms = 120000,
                   .tier1_temp_c = 120.0f,
                   .tier1_warning_ms = 60000,
                   .tier1_shutdown_ms = 150000,
                   .tier2_temp_c = 200.0f,
                   .tier2_rate_c_per_min = 20.0f,
                   .tier2_rate_min_temp_c = 150.0f,
                   .tier2_warning_ms = 30000,
                   .tier2_shutdown_ms = 120000,
                   .tiers_remote = true,
                   .paused = true,
                   .pause_used_ms = 3000,
                   .pause_budget_ms = 30000,
                   .pause_budget_cfg_ms = 30000,
                   .pause_budget_pct = 25,
                   .fusion_all = true,
                   .attendance_remote = true,
                   .cookware = "liquid",
                   .cookware_tau_s = 2400.0f,
                   .cookware_remote = true,
                   .presence_unknown = true,
                   .absent_ms = 400,
                   .loop_count = 3},
        .faults = faults,
        .fault_count = 1,
    };
}

static void test_radar_rule_fields(void)
{
    static const rb_json_node_t nodes[] = {
        {"node_01", "presence", "ONLINE", true, true, "dwelling", 6200, 4100, 35},
        {"node_02", "presence", "ONLINE", true, true, "passing", 900, 0, 120},
    };
    rb_telemetry_t t = sample();
    t.nodes = nodes;
    t.node_count = 2;
    t.safety.radar_rules = "observe";
    TEST_ASSERT_NOT_EQUAL(0, rb_json_telemetry(&t, buf, sizeof(buf)));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"verdict\":\"dwelling\",\"episode_ms\":6200,\"stationary_ms\":4100,\"span_cm\":35"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"verdict\":\"passing\",\"episode_ms\":900"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"radar_rules\":{\"mode\":\"observe\",\"quiet_gap_ms\":0,\"settle_window_ms\":0,\"source\":\"menuconfig\""));
    /* With the rules off, none of it is sent. */
    t = sample();
    TEST_ASSERT_NOT_EQUAL(0, rb_json_telemetry(&t, buf, sizeof(buf)));
    TEST_ASSERT_NULL(strstr(buf, "verdict"));
    TEST_ASSERT_NULL(strstr(buf, "radar_rules"));
}

static void test_unknown_values_are_null(void)
{
    const rb_telemetry_t t = sample();
    TEST_ASSERT_NOT_EQUAL(0, rb_json_telemetry(&t, buf, sizeof(buf)));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"timestamp\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"presence_state\":\"PRESENT\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"espnow_channel\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "{\"sensor_node\":\"node_01\",\"role\":\"presence\",\"link\":\"ONLINE\","
                                     "\"valid\":true,\"detected\":true}"));
    /* An offline radar's last reading is not reported as a person (or as nobody). */
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"link\":\"OFFLINE\",\"valid\":false,\"detected\":null}"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"role\":\"thermal\",\"link\":\"ONLINE\",\"valid\":true,\"detected\":null}"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"rate_c_per_min\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"faults\":[\"mqtt_disconnected\"]"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"schema_version\":2"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"controller_id\":\"controller_01\",\"boot_id\":\"3f9a01c2\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"shutdown\":true,\"reset_required\":true,\"warning_after_ms\":60000,"
                                     "\"shutdown_after_ms\":90000,\"shutdown_counts_from\":\"UNATTENDED\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"presence_filter\":{\"absence_ms\":2000,\"return_ms\":3000,"
                                     "\"return_gap_ms\":1000,\"fusion\":\"all\",\"pause_budget_cfg_ms\":30000,"
                                     "\"pause_budget_pct\":25,\"attendance_source\":\"dashboard\",\"source\":\"menuconfig\"},"
                                     "\"cooking\":{\"active\":true,\"phase\":\"cooling\",\"rate_c_per_min\":-12.34,"
                                     "\"settle_c\":null,\"on_c\":55.5,\"off_c\":45.5,\"source\":\"dashboard\"},"
                                     "\"cookware\":{\"profile\":\"liquid\",\"tau_s\":2400,\"source\":\"dashboard\"}}"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"hold\":\"cooling\",\"tier\":\"critical\",\"effective_warning_ms\":30000,"
                                     "\"effective_shutdown_ms\":120000,\"tiers\":{\"elevated\":{\"temp_c\":120.0,"
                                     "\"warning_ms\":60000,\"shutdown_ms\":150000},\"critical\":{\"temp_c\":200.0,"
                                     "\"rate_c_per_min\":20.0,\"rate_min_temp_c\":150.0,\"warning_ms\":30000,"
                                     "\"shutdown_ms\":120000},\"source\":\"dashboard\"},\"paused\":true,\"pause_used_ms\":3000,\"pause_budget_ms\":30000,"
                                     "\"presence_unknown\":true,\"thermal_lost\":false,"
                                     "\"absent_ms\":400,\"returning_ms\":0,"));

    TEST_ASSERT_NOT_EQUAL(0, rb_json_presence(&t, buf, sizeof(buf)));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"detected\":null")); /* invalid presence is not "false" */
    TEST_ASSERT_NOT_EQUAL(0, rb_json_node_status(&t, buf, sizeof(buf)));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"sensor_node\":\"node_02\",\"role\":\"presence\",\"link\":\"OFFLINE\","
                                     "\"valid\":false"));
}

static void test_telemetry_reports_espnow_channel(void)
{
    rb_telemetry_t t = sample();
    t.espnow_channel = 6;
    TEST_ASSERT_NOT_EQUAL(0, rb_json_telemetry(&t, buf, sizeof(buf)));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"protocol_version\":0,\"espnow_channel\":6,"));
}

static void test_controller_status_carries_boot_id(void)
{
    TEST_ASSERT_NOT_EQUAL(0, rb_json_controller_status("controller_01", "3f9a01c2", false, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("{\"schema_version\":2,\"type\":\"controller_status\",\"controller_id\":\"controller_01\","
                             "\"boot_id\":\"3f9a01c2\",\"online\":false}",
                             buf);
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
    rb_topic_build(RB_TOPIC_SENSOR_THERMAL_FRAME, "rb4107", 3, topic, sizeof(topic));
    TEST_ASSERT_EQUAL_STRING("rb4107/sensors/node_03/thermal_frame", topic);
    TEST_ASSERT_EQUAL(0, rb_topic_info(RB_TOPIC_SENSOR_THERMAL_FRAME)->qos);
    TEST_ASSERT_FALSE(rb_topic_info(RB_TOPIC_SENSOR_THERMAL_FRAME)->retain);
}

static void test_base64(void)
{
    static const struct {
        const char *in, *out;
    } CASES[] = {{"", "\"\""}, {"f", "\"Zg==\""}, {"fo", "\"Zm8=\""}, {"foo", "\"Zm9v\""},
                 {"foobar", "\"Zm9vYmFy\""}};
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        rb_json_writer_t w;
        rb_json_init(&w, buf, sizeof(buf));
        rb_json_base64(&w, (const uint8_t *)CASES[i].in, strlen(CASES[i].in));
        TEST_ASSERT_NOT_EQUAL(0, rb_json_finish(&w));
        TEST_ASSERT_EQUAL_STRING(CASES[i].out, buf);
    }
}

static void test_thermal_frame_fits_payload_buffer(void)
{
    static uint8_t pixels[32 * 24];
    static char big[1536]; /* PAYLOAD_LEN in rb_telemetry.c */
    for (size_t i = 0; i < sizeof(pixels); i++) {
        pixels[i] = (uint8_t)i;
    }
    /* Longest realistic header: a long controller ID, full timestamp, large counters. */
    const rb_json_thermal_frame_t f = {
        .hdr = {.controller_id = "controller_with_a_long_name_01", .boot_id = "3f9a01c2",
                .timestamp = "2026-10-02T23:59:59.999+08:00", .uptime_ms = 4000000000u, .sequence = 4000000000u},
        .sensor_node = "node_03",
        .frame_number = 4000000000u,
        .width = 32,
        .height = 24,
        .base_centi = -4000,
        .step_centi = 134,
        .invalid_value = 255,
        .hot_threshold_centi = 5000,
        .hot_region_radius = 1,
        .pixels = pixels,
    };
    const size_t len = rb_json_thermal_frame(&f, big, sizeof(big));
    TEST_ASSERT_NOT_EQUAL(0, len);
    TEST_ASSERT_TRUE(len < sizeof(big) - 64); /* some headroom left */
    TEST_ASSERT_NOT_NULL(strstr(big, "\"type\":\"thermal_frame\""));
    TEST_ASSERT_NOT_NULL(strstr(big, "\"sensor_node\":\"node_03\",\"frame\":{\"number\":4000000000,"
                                     "\"width\":32,\"height\":24,\"base_c\":-40.00,\"step_c\":1.34,"
                                     "\"invalid\":255,\"hot_threshold_c\":50.0,\"hot_region_radius\":1,\"encoding\":\"u8_base64\",\"pixels\":\"AAECAwQF"));
}

/* The state with every setting of rb_params.def, at its largest, still fits the payload buffer. */
static void test_state_with_params_fits_payload_buffer(void)
{
    static const char *keys[RB_PARAM_COUNT];
    static uint32_t values[RB_PARAM_COUNT];
    for (int i = 0; i < RB_PARAM_COUNT; i++) {
        keys[i] = RB_PARAMS[i].key;
        values[i] = RB_PARAMS[i].max;
    }
    rb_telemetry_t t = sample();
    t.hdr = (rb_json_header_t){.controller_id = "controller_with_a_long_name_01", .boot_id = "3f9a01c2",
                               .timestamp = "2026-10-02T23:59:59.999+08:00", .uptime_ms = 4000000000u,
                               .sequence = 4000000000u};
    t.safety.radar_rules = "observe";
    t.safety.param_keys = keys;
    t.safety.param_values = values;
    t.safety.param_count = RB_PARAM_COUNT;
    t.safety.params_remote = true;
    const size_t len = rb_json_telemetry(&t, buf, sizeof(buf));
    TEST_ASSERT_NOT_EQUAL(0, len);
    TEST_ASSERT_TRUE(len < sizeof(buf) - 512); /* room for more faults and nodes */
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"params\":{\"shutdown_timing\":1,"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"params_source\":\"dashboard\""));
}

void run_json_tests(void)
{
    RUN_TEST(test_state_with_params_fits_payload_buffer);
    RUN_TEST(test_unknown_values_are_null);
    RUN_TEST(test_radar_rule_fields);
    RUN_TEST(test_telemetry_reports_espnow_channel);
    RUN_TEST(test_controller_status_carries_boot_id);
    RUN_TEST(test_overflow_returns_zero);
    RUN_TEST(test_string_escaping);
    RUN_TEST(test_unbalanced_is_rejected);
    RUN_TEST(test_topics);
    RUN_TEST(test_base64);
    RUN_TEST(test_thermal_frame_fits_payload_buffer);
}
