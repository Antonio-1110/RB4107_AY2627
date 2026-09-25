/*
 * TODO section 4: shared ESP-NOW protocol.
 *
 * Runs the codec on the target: encodes each message type, decodes it back,
 * compares the fields, and checks that corrupted, truncated or mismatched
 * packets are rejected. It also prints each packet's size and a hex dump next
 * to the ESP-NOW limit. Flash it to either board (C6 or S3). The full unit
 * tests are in project 28.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_now.h"
#include "rb_protocol.h"

static const char *TAG = "PROTOCOL";

/* The protocol's own limit must agree with the IDF definition. */
_Static_assert(RB_ESPNOW_MAX_PAYLOAD == ESP_NOW_MAX_DATA_LEN, "RB_ESPNOW_MAX_PAYLOAD out of sync with ESP-IDF");

static int s_failures;

#define CHECK(cond)                                                   \
    do {                                                              \
        if (!(cond)) {                                                \
            ESP_LOGE(TAG, "FAIL line %d: %s", __LINE__, #cond);       \
            s_failures++;                                             \
        }                                                             \
    } while (0)

static bool near(float a, float b)
{
    return (isnan(a) && isnan(b)) || fabsf(a - b) < 0.006f;
}

static void hexdump(const char *label, const uint8_t *buf, size_t len)
{
    printf("%-13s %2u bytes:", label, (unsigned)len);
    for (size_t i = 0; i < len; i++) {
        printf(" %02x", buf[i]);
    }
    printf("\n");
}

static void test_sensor_data(void)
{
    rb_packet_t tx = {.type = RB_MSG_SENSOR_DATA, .node_id = 1, .sequence = 4127, .uptime_ms = 123456};
    tx.body.sensor.presence = (presence_reading_t){
        .valid = true, .presence_detected = true, .stationary_target = true, .distance_m = 1.23f, .timestamp_ms = 123400};
    tx.body.sensor.thermal = (thermal_reading_t){
        .valid = true, .max_temp_c = 184.25f, .min_temp_c = -3.5f, .mean_temp_c = 42.8f, .hot_region_temp_c = 170.0f,
        .temp_rate_c_per_min = NAN, .pixels_above_threshold = 37, .timestamp_ms = 123300};

    uint8_t buf[RB_PKT_MAX_LEN];
    size_t len = rb_protocol_encode(&tx, buf, sizeof(buf));
    CHECK(len == RB_PKT_SENSOR_DATA_LEN);
    hexdump("SENSOR_DATA", buf, len);

    rb_packet_t rx;
    CHECK(rb_protocol_decode(buf, len, &rx) == RB_DECODE_OK);
    CHECK(rx.node_id == 1 && rx.sequence == 4127 && rx.uptime_ms == 123456);
    CHECK(rx.body.sensor.presence.valid && rx.body.sensor.presence.stationary_target);
    CHECK(!rx.body.sensor.presence.moving_target);
    CHECK(near(rx.body.sensor.presence.distance_m, 1.23f));
    CHECK(rx.body.sensor.thermal.valid);
    CHECK(near(rx.body.sensor.thermal.max_temp_c, 184.25f));
    CHECK(near(rx.body.sensor.thermal.min_temp_c, -3.5f));
    CHECK(isnan(rx.body.sensor.thermal.temp_rate_c_per_min));
    CHECK(rx.body.sensor.thermal.pixels_above_threshold == 37);

    /* Corruption / mismatch must be rejected. */
    uint8_t bad[RB_PKT_MAX_LEN];
    memcpy(bad, buf, len);
    bad[20] ^= 0x01;
    CHECK(rb_protocol_decode(bad, len, &rx) == RB_DECODE_ERR_CRC);
    CHECK(rb_protocol_decode(buf, len - 1, &rx) == RB_DECODE_ERR_LENGTH);
    memcpy(bad, buf, len);
    bad[2] = RB_PROTOCOL_VERSION + 1;
    CHECK(rb_protocol_decode(bad, len, &rx) == RB_DECODE_ERR_VERSION);
    memcpy(bad, buf, len);
    bad[0] = 0;
    CHECK(rb_protocol_decode(bad, len, &rx) == RB_DECODE_ERR_MAGIC);
    memcpy(bad, buf, len);
    bad[3] = 0x7F;
    CHECK(rb_protocol_decode(bad, len, &rx) == RB_DECODE_ERR_TYPE);
}

static void test_heartbeat_and_fault(void)
{
    uint8_t buf[RB_PKT_MAX_LEN];
    rb_packet_t rx;

    rb_packet_t hb = {.type = RB_MSG_HEARTBEAT, .node_id = 1, .sequence = 7, .uptime_ms = 5000};
    hb.body.heartbeat = (rb_heartbeat_t){.fault_flags = RB_FAULT_MLX_NO_DATA, .tx_ok = 99, .tx_fail = 2};
    size_t len = rb_protocol_encode(&hb, buf, sizeof(buf));
    CHECK(len == RB_PKT_HEARTBEAT_LEN);
    hexdump("HEARTBEAT", buf, len);
    CHECK(rb_protocol_decode(buf, len, &rx) == RB_DECODE_OK);
    CHECK(rx.type == RB_MSG_HEARTBEAT && rx.body.heartbeat.fault_flags == RB_FAULT_MLX_NO_DATA);
    CHECK(rx.body.heartbeat.tx_ok == 99 && rx.body.heartbeat.tx_fail == 2);

    rb_packet_t f = {.type = RB_MSG_SENSOR_FAULT, .node_id = 1, .sequence = 8, .uptime_ms = 5100};
    f.body.fault = (rb_sensor_fault_msg_t){
        .fault_flags = RB_FAULT_C4002_NO_DATA | RB_FAULT_MLX_NO_DATA, .changed_flags = RB_FAULT_C4002_NO_DATA, .detail = -3};
    len = rb_protocol_encode(&f, buf, sizeof(buf));
    CHECK(len == RB_PKT_SENSOR_FAULT_LEN);
    hexdump("SENSOR_FAULT", buf, len);
    CHECK(rb_protocol_decode(buf, len, &rx) == RB_DECODE_OK);
    CHECK(rx.body.fault.changed_flags == RB_FAULT_C4002_NO_DATA && rx.body.fault.detail == -3);

    /* A packet of one type with the length of another is rejected. */
    CHECK(rb_protocol_decode(buf, RB_PKT_HEARTBEAT_LEN, &rx) == RB_DECODE_ERR_LENGTH);
}

void app_main(void)
{
    ESP_LOGI(TAG, "protocol v%u: SENSOR_DATA %u B, HEARTBEAT %u B, SENSOR_FAULT %u B (ESP-NOW limit %u B)",
             RB_PROTOCOL_VERSION, RB_PKT_SENSOR_DATA_LEN, RB_PKT_HEARTBEAT_LEN, RB_PKT_SENSOR_FAULT_LEN,
             ESP_NOW_MAX_DATA_LEN);
    test_sensor_data();
    test_heartbeat_and_fault();
    if (s_failures == 0) {
        ESP_LOGI(TAG, "all protocol checks PASSED");
    } else {
        ESP_LOGE(TAG, "%d protocol checks FAILED", s_failures);
    }
}
