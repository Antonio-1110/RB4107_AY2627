/* ESP-NOW protocol codec (section 4). */
#include <math.h>
#include <string.h>
#include "rb_protocol.h"
#include "unity.h"

static void test_presence_data_roundtrip(void)
{
    rb_packet_t tx = {.type = RB_MSG_PRESENCE_DATA, .role = RB_NODE_ROLE_PRESENCE, .node_id = 2, .sequence = 42,
                      .uptime_ms = 99999};
    tx.body.presence = (presence_reading_t){.valid = true, .presence_detected = true, .moving_target = true,
                                            .distance_m = 2.5f, .timestamp_ms = 99000};
    uint8_t buf[RB_PKT_MAX_LEN];
    const size_t len = rb_protocol_encode(&tx, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(RB_PKT_PRESENCE_DATA_LEN, len);
    rb_packet_t rx;
    TEST_ASSERT_EQUAL(RB_DECODE_OK, rb_protocol_decode(buf, len, &rx));
    TEST_ASSERT_EQUAL(RB_MSG_PRESENCE_DATA, rx.type);
    TEST_ASSERT_EQUAL(RB_NODE_ROLE_PRESENCE, rx.role);
    TEST_ASSERT_EQUAL_UINT32(2, rx.node_id);
    TEST_ASSERT_EQUAL_UINT32(42, rx.sequence);
    TEST_ASSERT_TRUE(rx.body.presence.valid);
    TEST_ASSERT_TRUE(rx.body.presence.presence_detected);
    TEST_ASSERT_TRUE(rx.body.presence.moving_target);
    TEST_ASSERT_FALSE(rx.body.presence.stationary_target);
    TEST_ASSERT_FLOAT_WITHIN(0.006f, 2.5f, rx.body.presence.distance_m);
    TEST_ASSERT_EQUAL_UINT32(99000, rx.body.presence.timestamp_ms);
}

static void test_thermal_data_roundtrip(void)
{
    rb_packet_t tx = {.type = RB_MSG_THERMAL_DATA, .role = RB_NODE_ROLE_THERMAL, .node_id = 3, .sequence = 7,
                      .uptime_ms = 5000};
    tx.body.thermal = (thermal_reading_t){.valid = true, .max_temp_c = 250.25f, .min_temp_c = -10.5f,
                                          .mean_temp_c = 30.0f, .hot_region_temp_c = 200.0f,
                                          .temp_rate_c_per_min = NAN, .pixels_above_threshold = 65,
                                          .timestamp_ms = 98000};
    uint8_t buf[RB_PKT_MAX_LEN];
    const size_t len = rb_protocol_encode(&tx, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(RB_PKT_THERMAL_DATA_LEN, len);
    rb_packet_t rx;
    TEST_ASSERT_EQUAL(RB_DECODE_OK, rb_protocol_decode(buf, len, &rx));
    TEST_ASSERT_EQUAL(RB_NODE_ROLE_THERMAL, rx.role);
    TEST_ASSERT_TRUE(rx.body.thermal.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.006f, 250.25f, rx.body.thermal.max_temp_c);
    TEST_ASSERT_FLOAT_WITHIN(0.006f, -10.5f, rx.body.thermal.min_temp_c);
    TEST_ASSERT_FLOAT_WITHIN(0.006f, 200.0f, rx.body.thermal.hot_region_temp_c);
    TEST_ASSERT_TRUE(isnan(rx.body.thermal.temp_rate_c_per_min));
    TEST_ASSERT_EQUAL_UINT16(65, rx.body.thermal.pixels_above_threshold);
    TEST_ASSERT_EQUAL_UINT32(98000, rx.body.thermal.timestamp_ms);
}

static void test_unknown_and_saturated_values(void)
{
    rb_packet_t tx = {.type = RB_MSG_PRESENCE_DATA, .role = RB_NODE_ROLE_PRESENCE};
    tx.body.presence.distance_m = NAN;
    uint8_t buf[RB_PKT_MAX_LEN];
    rb_packet_t rx;
    TEST_ASSERT_EQUAL(RB_DECODE_OK, rb_protocol_decode(buf, rb_protocol_encode(&tx, buf, sizeof(buf)), &rx));
    TEST_ASSERT_TRUE(isnan(rx.body.presence.distance_m));

    tx = (rb_packet_t){.type = RB_MSG_THERMAL_DATA, .role = RB_NODE_ROLE_THERMAL};
    tx.body.thermal.max_temp_c = 1000.0f; /* beyond the int16 centi-degree range */
    tx.body.thermal.min_temp_c = -1000.0f;
    TEST_ASSERT_EQUAL(RB_DECODE_OK, rb_protocol_decode(buf, rb_protocol_encode(&tx, buf, sizeof(buf)), &rx));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 327.67f, rx.body.thermal.max_temp_c);
    TEST_ASSERT_FALSE(isnan(rx.body.thermal.min_temp_c)); /* saturates, never becomes "unknown" */
}

static void test_role_must_match_data(void)
{
    uint8_t buf[RB_PKT_MAX_LEN];
    /* The encoder refuses a thermal node sending presence data (and vice versa). */
    rb_packet_t tx = {.type = RB_MSG_PRESENCE_DATA, .role = RB_NODE_ROLE_THERMAL};
    TEST_ASSERT_EQUAL(0, rb_protocol_encode(&tx, buf, sizeof(buf)));
    tx = (rb_packet_t){.type = RB_MSG_HEARTBEAT, .role = (rb_node_role_t)9};
    TEST_ASSERT_EQUAL(0, rb_protocol_encode(&tx, buf, sizeof(buf)));

    /* The decoder rejects it too, even with a correct CRC. */
    tx = (rb_packet_t){.type = RB_MSG_THERMAL_DATA, .role = RB_NODE_ROLE_THERMAL};
    const size_t len = rb_protocol_encode(&tx, buf, sizeof(buf));
    buf[4] = RB_NODE_ROLE_PRESENCE;
    const uint16_t crc = rb_crc16(buf, len - RB_CRC_LEN);
    buf[len - 2] = crc & 0xFF;
    buf[len - 1] = crc >> 8;
    rb_packet_t rx;
    TEST_ASSERT_EQUAL(RB_DECODE_ERR_ROLE, rb_protocol_decode(buf, len, &rx));
}

static void test_rejects_corruption(void)
{
    rb_packet_t tx = {.type = RB_MSG_HEARTBEAT, .role = RB_NODE_ROLE_PRESENCE, .node_id = 1, .sequence = 5};
    uint8_t buf[RB_PKT_MAX_LEN], bad[RB_PKT_MAX_LEN];
    const size_t len = rb_protocol_encode(&tx, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(RB_PKT_HEARTBEAT_LEN, len);
    rb_packet_t rx;
    for (size_t i = 0; i < len; i++) { /* flip each byte in turn: every corruption is caught */
        memcpy(bad, buf, len);
        bad[i] ^= 0x40;
        TEST_ASSERT_NOT_EQUAL(RB_DECODE_OK, rb_protocol_decode(bad, len, &rx));
    }
    TEST_ASSERT_EQUAL(RB_DECODE_ERR_LENGTH, rb_protocol_decode(buf, len + 1, &rx));
    TEST_ASSERT_EQUAL(RB_DECODE_ERR_LENGTH, rb_protocol_decode(buf, 3, &rx));
    TEST_ASSERT_EQUAL(RB_DECODE_ERR_LENGTH, rb_protocol_decode(NULL, 0, &rx));
}

static void test_encode_rejects_bad_input(void)
{
    rb_packet_t tx = {.type = (rb_msg_type_t)99, .role = RB_NODE_ROLE_PRESENCE};
    uint8_t buf[RB_PKT_MAX_LEN];
    TEST_ASSERT_EQUAL(0, rb_protocol_encode(&tx, buf, sizeof(buf)));
    tx.type = RB_MSG_PRESENCE_DATA;
    TEST_ASSERT_EQUAL(0, rb_protocol_encode(&tx, buf, RB_PKT_PRESENCE_DATA_LEN - 1));
}

void run_protocol_tests(void)
{
    RUN_TEST(test_presence_data_roundtrip);
    RUN_TEST(test_thermal_data_roundtrip);
    RUN_TEST(test_unknown_and_saturated_values);
    RUN_TEST(test_role_must_match_data);
    RUN_TEST(test_rejects_corruption);
    RUN_TEST(test_encode_rejects_bad_input);
}
