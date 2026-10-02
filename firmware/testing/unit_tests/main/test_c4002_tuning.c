/* Remote C4002 tuning: ESP-NOW messages, parameter checks, MQTT command parser, reply JSON. */
#include <string.h>
#include "rb_c4002_cmd.h"
#include "rb_protocol.h"
#include "unity.h"

static rb_c4002_params_t defaults(void)
{
    return (rb_c4002_params_t){
        .report_period_ds = 5,
        .range_max_cm = 1100,
        .motion_sensitivity = RB_C4002_SENS_MID,
        .presence_sensitivity = RB_C4002_SENS_MID,
        .disappear_delay_s = 1,
        .lock_time_ds = 10,
        .motion_gate_mask = RB_C4002_ALL_GATES,
        .presence_gate_mask = RB_C4002_ALL_GATES,
    };
}

static bool parse(const char *json, rb_c4002_cmd_t *cmd, char *err)
{
    return rb_c4002_cmd_parse(json, strlen(json), cmd, err, 64);
}

static void test_sizes_fit_espnow(void)
{
    TEST_ASSERT_EQUAL(102, RB_PKT_C4002_CONFIG_LEN);
    TEST_ASSERT_EQUAL(96, RB_PKT_C4002_CONFIG_ACK_LEN);
    TEST_ASSERT_TRUE(RB_PKT_MAX_LEN <= RB_ESPNOW_MAX_PAYLOAD);
}

static void test_config_roundtrip(void)
{
    rb_packet_t tx = {.type = RB_MSG_C4002_CONFIG, .role = RB_NODE_ROLE_CONTROLLER, .sequence = 9};
    rb_c4002_config_msg_t *c = &tx.body.c4002_config;
    *c = (rb_c4002_config_msg_t){.target_node_id = 2, .request_id = 4321, .action = RB_C4002_ACTION_APPLY,
                                 .field_mask = RB_C4002_F_RANGE_MAX | RB_C4002_F_PRESENCE_THRESH,
                                 .calib_delay_s = 10, .calib_duration_s = 30, .params = defaults()};
    c->params.range_max_cm = 350;
    c->params.presence_gate_mask = 0x01FFFF7Fu; /* gate 7 off */
    for (unsigned i = 0; i < RB_C4002_MAX_GATES; i++) {
        c->params.presence_thresholds[i] = (uint8_t)(i * 3);
    }
    uint8_t buf[RB_PKT_MAX_LEN];
    const size_t len = rb_protocol_encode(&tx, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(RB_PKT_C4002_CONFIG_LEN, len);
    rb_packet_t rx;
    TEST_ASSERT_EQUAL(RB_DECODE_OK, rb_protocol_decode(buf, len, &rx));
    TEST_ASSERT_EQUAL(RB_NODE_ROLE_CONTROLLER, rx.role);
    TEST_ASSERT_EQUAL_MEMORY(c, &rx.body.c4002_config, sizeof(*c));
}

static void test_ack_roundtrip(void)
{
    rb_packet_t tx = {.type = RB_MSG_C4002_CONFIG_ACK, .role = RB_NODE_ROLE_PRESENCE, .node_id = 1};
    tx.body.c4002_ack = (rb_c4002_ack_t){.request_id = 7, .action = RB_C4002_ACTION_CALIBRATE,
                                         .result = RB_C4002_RESULT_OK, .calib_remaining_s = 25, .saved = 1,
                                         .params = defaults()};
    tx.body.c4002_ack.params.motion_thresholds[24] = 99;
    uint8_t buf[RB_PKT_MAX_LEN];
    const size_t len = rb_protocol_encode(&tx, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(RB_PKT_C4002_CONFIG_ACK_LEN, len);
    rb_packet_t rx;
    TEST_ASSERT_EQUAL(RB_DECODE_OK, rb_protocol_decode(buf, len, &rx));
    TEST_ASSERT_EQUAL_MEMORY(&tx.body.c4002_ack, &rx.body.c4002_ack, sizeof(rb_c4002_ack_t));
}

static void test_roles_are_enforced(void)
{
    uint8_t buf[RB_PKT_MAX_LEN];
    /* A sensor node can't send a config command, and the controller can't send an ACK. */
    rb_packet_t pkt = {.type = RB_MSG_C4002_CONFIG, .role = RB_NODE_ROLE_PRESENCE};
    pkt.body.c4002_config.params = defaults();
    TEST_ASSERT_EQUAL(0, rb_protocol_encode(&pkt, buf, sizeof(buf)));
    pkt.type = RB_MSG_C4002_CONFIG_ACK;
    pkt.role = RB_NODE_ROLE_CONTROLLER;
    TEST_ASSERT_EQUAL(0, rb_protocol_encode(&pkt, buf, sizeof(buf)));
    /* The thermal node has no C4002. */
    pkt.role = RB_NODE_ROLE_THERMAL;
    TEST_ASSERT_EQUAL(0, rb_protocol_encode(&pkt, buf, sizeof(buf)));
}

static void test_params_check(void)
{
    rb_c4002_params_t p = defaults();
    TEST_ASSERT_NULL(rb_c4002_params_check(&p));
    p.range_min_cm = 500;
    p.range_max_cm = 300;
    TEST_ASSERT_NOT_NULL(rb_c4002_params_check(&p));
    p = defaults();
    p.lock_time_ds = 1;
    TEST_ASSERT_NOT_NULL(rb_c4002_params_check(&p));
    p = defaults();
    p.presence_sensitivity = RB_C4002_SENS_CUSTOM; /* no thresholds known yet */
    TEST_ASSERT_NOT_NULL(rb_c4002_params_check(&p));
    p.thresholds_known = RB_C4002_THRESH_PRESENCE;
    TEST_ASSERT_NULL(rb_c4002_params_check(&p));
    p.presence_thresholds[3] = 100;
    TEST_ASSERT_NOT_NULL(rb_c4002_params_check(&p));
    p = defaults();
    p.motion_gate_mask = 1u << 25;
    TEST_ASSERT_NOT_NULL(rb_c4002_params_check(&p));
}

static void test_merge_only_masked_fields(void)
{
    rb_c4002_params_t cur = defaults(), in = {0};
    in.range_max_cm = 300;
    in.lock_time_ds = 50;               /* not in the mask: must be ignored */
    in.motion_thresholds[0] = 42;
    rb_c4002_params_merge(&cur, &in, RB_C4002_F_RANGE_MAX | RB_C4002_F_MOTION_THRESH);
    TEST_ASSERT_EQUAL(300, cur.range_max_cm);
    TEST_ASSERT_EQUAL(10, cur.lock_time_ds);
    TEST_ASSERT_EQUAL(42, cur.motion_thresholds[0]);
    TEST_ASSERT_EQUAL(RB_C4002_SENS_CUSTOM, cur.motion_sensitivity);
    TEST_ASSERT_EQUAL(RB_C4002_SENS_MID, cur.presence_sensitivity);
    TEST_ASSERT_TRUE(cur.thresholds_known & RB_C4002_THRESH_MOTION);
    TEST_ASSERT_NULL(rb_c4002_params_check(&cur));
}

static void test_cmd_parse_apply(void)
{
    rb_c4002_cmd_t cmd;
    char err[64];
    TEST_ASSERT_TRUE_MESSAGE(parse("{\"schema_version\": 2, \"type\": \"c4002_command\", \"request_id\": 77,"
                                   " \"controller_id\": \"controller_01\", \"action\": \"apply\","
                                   " \"range_min_cm\": 30, \"range_max_cm\": 300, \"resolution_cm\": 20,"
                                   " \"presence_sensitivity\": \"low\", \"lock_time_ds\": 20,"
                                   " \"presence_gates\": [1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1]}",
                                   &cmd, err),
                             err);
    TEST_ASSERT_EQUAL(77, cmd.msg.request_id);
    TEST_ASSERT_EQUAL(RB_C4002_ACTION_APPLY, cmd.msg.action);
    TEST_ASSERT_EQUAL_STRING("controller_01", cmd.controller_id);
    TEST_ASSERT_EQUAL_HEX16(RB_C4002_F_RANGE_MIN | RB_C4002_F_RANGE_MAX | RB_C4002_F_RESOLUTION |
                                RB_C4002_F_PRESENCE_SENS | RB_C4002_F_LOCK_TIME | RB_C4002_F_PRESENCE_GATES,
                            cmd.msg.field_mask);
    TEST_ASSERT_EQUAL(30, cmd.msg.params.range_min_cm);
    TEST_ASSERT_EQUAL(300, cmd.msg.params.range_max_cm);
    TEST_ASSERT_EQUAL(RB_C4002_RES_20CM, cmd.msg.params.resolution);
    TEST_ASSERT_EQUAL(RB_C4002_SENS_LOW, cmd.msg.params.presence_sensitivity);
    TEST_ASSERT_EQUAL_HEX32(0x01FFFF7Fu, cmd.msg.params.presence_gate_mask);
}

static void test_cmd_parse_calibrate_defaults(void)
{
    rb_c4002_cmd_t cmd;
    char err[64];
    TEST_ASSERT_TRUE(parse("{\"type\":\"c4002_command\",\"request_id\":5,\"action\":\"calibrate\"}", &cmd, err));
    TEST_ASSERT_EQUAL(RB_C4002_ACTION_CALIBRATE, cmd.msg.action);
    TEST_ASSERT_EQUAL(RB_C4002_CMD_DEFAULT_CALIB_DELAY_S, cmd.msg.calib_delay_s);
    TEST_ASSERT_EQUAL(RB_C4002_CMD_DEFAULT_CALIB_DURATION_S, cmd.msg.calib_duration_s);
    TEST_ASSERT_TRUE(parse("{\"type\":\"c4002_command\",\"request_id\":5,\"action\":\"calibrate\","
                           "\"calibration_delay_s\":0,\"calibration_duration_s\":120}",
                           &cmd, err));
    TEST_ASSERT_EQUAL(0, cmd.msg.calib_delay_s);
    TEST_ASSERT_EQUAL(120, cmd.msg.calib_duration_s);
}

static void test_cmd_parse_rejects(void)
{
    rb_c4002_cmd_t cmd;
    char err[64];
    static const char *const bad[] = {
        "",
        "[]",
        "{\"type\":\"c4002_command\",\"action\":\"apply\"}",                        /* apply without settings */
        "{\"type\":\"c4002_command\",\"action\":\"read\",\"range_max_cm\":300}",    /* settings need apply */
        "{\"type\":\"telemetry\",\"action\":\"read\"}",
        "{\"action\":\"read\"}",                                                   /* no type */
        "{\"type\":\"c4002_command\",\"action\":\"explode\"}",
        "{\"type\":\"c4002_command\",\"action\":\"apply\",\"rang_max_cm\":300}",   /* typo */
        "{\"type\":\"c4002_command\",\"action\":\"apply\",\"range_max_cm\":2000}",
        "{\"type\":\"c4002_command\",\"action\":\"apply\",\"range_max_cm\":3.5}",
        "{\"type\":\"c4002_command\",\"action\":\"apply\",\"range_min_cm\":500,\"range_max_cm\":300}",
        "{\"type\":\"c4002_command\",\"action\":\"apply\",\"resolution_cm\":40}",
        "{\"type\":\"c4002_command\",\"action\":\"apply\",\"motion_sensitivity\":\"max\"}",
        "{\"type\":\"c4002_command\",\"action\":\"apply\",\"motion_gates\":[1,0,1]}",
        "{\"type\":\"c4002_command\",\"action\":\"apply\",\"motion_thresholds\":"
        "[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,100]}",
        "{\"type\":\"c4002_command\",\"action\":\"calibrate\",\"calibration_duration_s\":0}",
        "{\"type\":\"c4002_command\",\"action\":\"read\"} trailing",
        "{\"type\":\"c4002_command\",\"action\":\"read\"",
        "{\"type\":\"c4002_\\u0063ommand\",\"action\":\"read\"}",                  /* escapes not supported */
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        err[0] = '\0';
        TEST_ASSERT_FALSE_MESSAGE(parse(bad[i], &cmd, err), bad[i]);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0, strlen(err), bad[i]);
    }
}

static void test_cmd_topic(void)
{
    uint32_t id = 0;
    TEST_ASSERT_TRUE(rb_c4002_cmd_topic_node("rb4107/sensors/node_02/c4002_set", "rb4107", &id));
    TEST_ASSERT_EQUAL(2, id);
    TEST_ASSERT_FALSE(rb_c4002_cmd_topic_node("rb4107/sensors/node_02/presence", "rb4107", &id));
    TEST_ASSERT_FALSE(rb_c4002_cmd_topic_node("other/sensors/node_02/c4002_set", "rb4107", &id));
    TEST_ASSERT_FALSE(rb_c4002_cmd_topic_node("rb4107/sensors/node_/c4002_set", "rb4107", &id));
}

static void test_reply_json(void)
{
    char buf[1536];
    rb_c4002_reply_t r = {
        .hdr = {.controller_id = "controller_01", .boot_id = "b00t", .uptime_ms = 1000, .sequence = 3},
        .sensor_node = "node_01",
        .kind = RB_C4002_REPLY_NODE,
        .request_id = 77,
        .action = RB_C4002_ACTION_APPLY,
        .has_ack = true,
        .ack = {.request_id = 77, .action = RB_C4002_ACTION_APPLY, .saved = 1, .params = defaults()},
    };
    size_t len = rb_c4002_reply_json(&r, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"type\":\"c4002_config\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"result\":\"ok\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"presence_sensitivity\":\"mid\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"motion_thresholds\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"saved\":true"));

    r.kind = RB_C4002_REPLY_NO_REPLY;
    r.has_ack = false;
    r.error = "the node did not answer";
    len = rb_c4002_reply_json(&r, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"result\":\"no_reply\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"settings\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"saved\":null"));
}

void run_c4002_tuning_tests(void)
{
    RUN_TEST(test_sizes_fit_espnow);
    RUN_TEST(test_config_roundtrip);
    RUN_TEST(test_ack_roundtrip);
    RUN_TEST(test_roles_are_enforced);
    RUN_TEST(test_params_check);
    RUN_TEST(test_merge_only_masked_fields);
    RUN_TEST(test_cmd_parse_apply);
    RUN_TEST(test_cmd_parse_calibrate_defaults);
    RUN_TEST(test_cmd_parse_rejects);
    RUN_TEST(test_cmd_topic);
    RUN_TEST(test_reply_json);
}
