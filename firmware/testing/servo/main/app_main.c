/*
 * Valve node bring-up: the servo on its own.
 *
 * Moves between the CLOSED and OPEN angles every 3 s, using the same
 * menuconfig settings as firmware/valve_node. Check that the valve handle
 * turns the right way, reaches both ends, and that the servo doesn't buzz or
 * strain at either end.
 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rb_config.h"
#include "servo.h"

static const char *TAG = "SERVO";

#define HOLD_MS 3000

void app_main(void)
{
    const servo_config_t cfg = {
        .gpio = CONFIG_RB_VALVE_SERVO_GPIO,
        .min_pulse_us = CONFIG_RB_VALVE_SERVO_MIN_PULSE_US,
        .max_pulse_us = CONFIG_RB_VALVE_SERVO_MAX_PULSE_US,
        .range_deg = CONFIG_RB_VALVE_SERVO_RANGE_DEG,
    };
    ESP_ERROR_CHECK(servo_init(&cfg));

    bool open = false;
    for (;;) {
        const uint32_t angle = open ? CONFIG_RB_VALVE_OPEN_ANGLE_DEG : CONFIG_RB_VALVE_CLOSED_ANGLE_DEG;
        ESP_ERROR_CHECK(servo_set_angle(angle));
        ESP_LOGI(TAG, "%s: %lu deg, pulse %lu us", open ? "OPEN" : "CLOSED", (unsigned long)angle,
                 (unsigned long)servo_pulse_us(angle));
        vTaskDelay(pdMS_TO_TICKS(HOLD_MS));
        open = !open;
    }
}
