#include "rb_sim.h"

#include <math.h>
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rb_controller.h"
#include "rb_protocol.h"
#include "rb_time.h"

static const char *TAG = "SIM";

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static rb_sim_inputs_t s_in;
static uint32_t s_node_id;
static uint32_t s_period_ms;
static uint32_t s_sequence;

rb_sim_inputs_t rb_sim_defaults(void)
{
    return (rb_sim_inputs_t){
        .node_online = true,
        .presence_valid = true,
        .person_present = true,
        .thermal_valid = true,
        .hot_region_c = 25.0f,
        .rate_c_per_min = 0.0f,
    };
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

static void inject(rb_packet_t *pkt)
{
    pkt->protocol_version = RB_PROTOCOL_VERSION;
    pkt->node_id = s_node_id;
    pkt->sequence = ++s_sequence;
    pkt->uptime_ms = rb_time_mono_ms();
    rb_controller_inject(pkt);
}

static void sim_task(void *arg)
{
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(s_period_ms));
        rb_sim_inputs_t in;
        rb_sim_get(&in);
        if (!in.node_online) {
            continue;
        }
        const uint32_t now = rb_time_mono_ms();
        rb_packet_t data = {.type = RB_MSG_SENSOR_DATA};
        data.body.sensor.presence = (presence_reading_t){
            .valid = in.presence_valid,
            .presence_detected = in.presence_valid && in.person_present,
            .stationary_target = in.presence_valid && in.person_present,
            .distance_m = in.presence_valid && in.person_present ? 0.8f : NAN,
            .timestamp_ms = now,
        };
        data.body.sensor.thermal = (thermal_reading_t){
            .valid = in.thermal_valid,
            .max_temp_c = in.hot_region_c + 5.0f,
            .min_temp_c = 22.0f,
            .mean_temp_c = 26.0f,
            .hot_region_temp_c = in.hot_region_c,
            .temp_rate_c_per_min = in.rate_c_per_min,
            .pixels_above_threshold = in.hot_region_c > 50.0f ? 12 : 0,
            .timestamp_ms = now,
        };
        inject(&data);

        rb_packet_t hb = {.type = RB_MSG_HEARTBEAT};
        hb.body.heartbeat.fault_flags = (in.presence_valid ? 0 : RB_FAULT_C4002_INVALID) |
                                        (in.thermal_valid ? 0 : RB_FAULT_MLX_INVALID);
        inject(&hb);
    }
}

esp_err_t rb_sim_start(uint32_t node_id, uint32_t period_ms)
{
    s_node_id = node_id;
    s_period_ms = period_ms;
    s_in = rb_sim_defaults();
    ESP_LOGW(TAG, "SIMULATED sensor node %lu active (no ESP-NOW)", (unsigned long)node_id);
    return xTaskCreate(sim_task, "sim_node", 3072, NULL, 6, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

typedef struct {
    const rb_sim_step_t *steps;
    size_t count;
    bool loop;
} script_t;

static void script_task(void *arg)
{
    const script_t *sc = arg;
    do {
        const TickType_t start = xTaskGetTickCount();
        for (size_t i = 0; i < sc->count; i++) {
            const TickType_t at = start + pdMS_TO_TICKS(sc->steps[i].at_ms);
            const TickType_t now = xTaskGetTickCount();
            if ((int32_t)(at - now) > 0) {
                vTaskDelay(at - now);
            }
            ESP_LOGW(TAG, "scenario t=%lus: %s", (unsigned long)(sc->steps[i].at_ms / 1000), sc->steps[i].label);
            rb_sim_set(&sc->steps[i].inputs);
            if (sc->steps[i].press_reset) {
                rb_controller_request_reset();
            }
        }
    } while (sc->loop);
    vTaskDelete(NULL);
}

esp_err_t rb_sim_run_script(const rb_sim_step_t *steps, size_t count, bool loop)
{
    static script_t sc;
    sc = (script_t){steps, count, loop};
    return xTaskCreate(script_task, "sim_script", 3072, &sc, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
