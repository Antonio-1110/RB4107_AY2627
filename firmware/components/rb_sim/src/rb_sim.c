#include "rb_sim.h"

#include <math.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rb_controller.h"
#include "rb_protocol.h"
#include "rb_time.h"

static const char *TAG = "SIM";

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static rb_sim_inputs_t s_in;
static uint32_t s_node_ids[RB_SIM_NODE_COUNT];
static bool s_enabled[RB_SIM_NODE_COUNT];
static uint32_t s_sequence[RB_SIM_NODE_COUNT];
static uint32_t s_period_ms;

rb_sim_inputs_t rb_sim_defaults(void)
{
    rb_sim_inputs_t in = {
        .person_present = true,
        .hot_region_c = 25.0f,
        .rate_c_per_min = 0.0f,
    };
    for (int i = 0; i < RB_SIM_NODE_COUNT; i++) {
        in.online[i] = true;
        in.valid[i] = true;
    }
    return in;
}

void rb_sim_set(const rb_sim_inputs_t *inputs)
{
    portENTER_CRITICAL(&s_lock);
    s_in = *inputs;
    portEXIT_CRITICAL(&s_lock);
}

void rb_sim_get(rb_sim_inputs_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_in;
    portEXIT_CRITICAL(&s_lock);
}

static void inject(rb_sim_node_t node, rb_packet_t *pkt)
{
    pkt->protocol_version = RB_PROTOCOL_VERSION;
    pkt->role = node == RB_SIM_THERMAL ? RB_NODE_ROLE_THERMAL : RB_NODE_ROLE_PRESENCE;
    pkt->node_id = s_node_ids[node];
    pkt->sequence = ++s_sequence[node];
    pkt->uptime_ms = rb_time_mono_ms();
    rb_controller_inject(pkt);
}

static void send_presence(rb_sim_node_t node, const rb_sim_inputs_t *in, uint32_t now)
{
    const bool valid = in->valid[node];
    const bool present = valid && in->person_present;
    rb_packet_t data = {.type = RB_MSG_PRESENCE_DATA};
    data.body.presence = (presence_reading_t){
        .valid = valid,
        .presence_detected = present,
        .stationary_target = present,
        .distance_m = present ? 0.8f : NAN,
        .timestamp_ms = now,
    };
    inject(node, &data);

    rb_packet_t hb = {.type = RB_MSG_HEARTBEAT};
    hb.body.heartbeat.fault_flags = valid ? 0 : RB_FAULT_C4002_INVALID;
    inject(node, &hb);
}

static void send_thermal(const rb_sim_inputs_t *in, uint32_t now)
{
    const bool valid = in->valid[RB_SIM_THERMAL];
    rb_packet_t data = {.type = RB_MSG_THERMAL_DATA};
    data.body.thermal = (thermal_reading_t){
        .valid = valid,
        .max_temp_c = in->hot_region_c + 5.0f,
        .min_temp_c = 22.0f,
        .mean_temp_c = 26.0f,
        .hot_region_temp_c = in->hot_region_c,
        .temp_rate_c_per_min = in->rate_c_per_min,
        .pixels_above_threshold = in->hot_region_c > 50.0f ? 12 : 0,
        .timestamp_ms = now,
    };
    inject(RB_SIM_THERMAL, &data);

    rb_packet_t hb = {.type = RB_MSG_HEARTBEAT};
    hb.body.heartbeat.fault_flags = valid ? 0 : RB_FAULT_MLX_INVALID;
    inject(RB_SIM_THERMAL, &hb);
}

static void sim_task(void *arg)
{
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(s_period_ms));
        rb_sim_inputs_t in;
        rb_sim_get(&in);
        const uint32_t now = rb_time_mono_ms();
        for (int node = 0; node < RB_SIM_NODE_COUNT; node++) {
            if (!s_enabled[node] || !in.online[node]) {
                continue;
            }
            if (node == RB_SIM_THERMAL) {
                send_thermal(&in, now);
            } else {
                send_presence((rb_sim_node_t)node, &in, now);
            }
        }
    }
}

esp_err_t rb_sim_start(const uint32_t presence_ids[2], uint8_t presence_count, uint32_t thermal_id, uint32_t period_ms)
{
    s_node_ids[RB_SIM_PRESENCE_A] = presence_ids[0];
    s_node_ids[RB_SIM_PRESENCE_B] = presence_ids[1];
    s_node_ids[RB_SIM_THERMAL] = thermal_id;
    s_enabled[RB_SIM_PRESENCE_A] = true;
    s_enabled[RB_SIM_PRESENCE_B] = presence_count > 1;
    s_enabled[RB_SIM_THERMAL] = true;
    s_period_ms = period_ms;
    s_in = rb_sim_defaults();
    if (presence_count > 1) {
        ESP_LOGW(TAG, "SIMULATED sensor nodes active (no ESP-NOW): presence node_%02lu + node_%02lu, thermal node_%02lu",
                 (unsigned long)presence_ids[0], (unsigned long)presence_ids[1], (unsigned long)thermal_id);
    } else {
        ESP_LOGW(TAG, "SIMULATED sensor nodes active (no ESP-NOW): presence node_%02lu, thermal node_%02lu",
                 (unsigned long)presence_ids[0], (unsigned long)thermal_id);
    }
    return xTaskCreate(sim_task, "sim_node", 3072, NULL, 6, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
