/*
 * TODO section 21: MQTT JSON schema.
 *
 * Serialises one example of every payload type with the on-target JSON
 * writer and prints each as a single line. Pipe the monitor output through
 * the schema validator to check them:
 *
 *   idf.py monitor | python3 ../../tools/diagnostics/validate_json.py
 *
 * The examples include the awkward cases: unknown values (null), invalid
 * presence, strings that need escaping, and a missing wall-clock time.
 */
#include <math.h>
#include <stdio.h>
#include "esp_log.h"
#include "rb_config.h"
#include "rb_json.h"

static const char *TAG = "JSON";

static char s_buf[1024];

static void emit(const char *what, size_t len)
{
    if (len == 0) {
        ESP_LOGE(TAG, "%s: did not fit in %u bytes", what, (unsigned)sizeof(s_buf));
        return;
    }
    printf("%s\n", s_buf);
}

void app_main(void)
{
    const rb_json_fault_t faults[] = {{"mqtt_disconnected", "TELEMETRY"}};
    rb_telemetry_t t = {
        .hdr = {CONFIG_RB_MQTT_CONTROLLER_ID, "2026-09-26T00:00:00+08:00", 18400, 4127},
        .protocol_version = 1,
        .sensor_node = "node_01",
        .node = {"ONLINE", 0, 0, 0},
        .presence = {.valid = true, .detected = false, .distance_m = NAN},
        .thermal = {true, 84.2f, 21.0f, 42.8f, 80.1f, 1.7f, 37},
        .safety = {"UNATTENDED", 16400, 18400, "OFF", false, false, 184},
        .faults = faults,
        .fault_count = 1,
    };
    ESP_LOGI(TAG, "schema_version %d examples:", RB_JSON_SCHEMA_VERSION);
    emit("telemetry", rb_json_telemetry(&t, s_buf, sizeof(s_buf)));
    emit("heartbeat", rb_json_heartbeat(&t, s_buf, sizeof(s_buf)));
    emit("faults", rb_json_faults(&t, s_buf, sizeof(s_buf)));
    emit("thermal", rb_json_thermal(&t, s_buf, sizeof(s_buf)));

    /* Node gone: presence unknown must come out as null, not false. */
    t.presence.valid = false;
    t.node.link = "OFFLINE";
    t.hdr.timestamp = NULL; /* wall clock not set yet */
    emit("presence", rb_json_presence(&t, s_buf, sizeof(s_buf)));
    emit("node_status", rb_json_node_status(&t, s_buf, sizeof(s_buf)));

    const rb_event_msg_t warning = {
        .hdr = {CONFIG_RB_MQTT_CONTROLLER_ID, "2026-09-26T00:01:00+08:00", 78400, 4128},
        .event = "warning",
        .state = "WARNING",
        .from_state = "UNATTENDED",
        .reason = "unattended timeout",
        .unattended_ms = 60000,
    };
    emit("event", rb_json_event(&warning, s_buf, sizeof(s_buf)));
    const rb_event_msg_t fault = {
        .hdr = {CONFIG_RB_MQTT_CONTROLLER_ID, NULL, 78500, 4129},
        .event = "fault_raised",
        .state = "WARNING",
        .reason = "broker \"unreachable\"\n",
        .fault = "mqtt_disconnected",
        .fault_class = "TELEMETRY",
    };
    emit("event", rb_json_event(&fault, s_buf, sizeof(s_buf)));
    emit("controller_status", rb_json_controller_status(CONFIG_RB_MQTT_CONTROLLER_ID, true, s_buf, sizeof(s_buf)));
    ESP_LOGI(TAG, "done");
}
