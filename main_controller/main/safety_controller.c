#include "safety_controller.h"

#include "buzzer.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "relay_control.h"
#include "sensor_manager.h"
#include "system_config.h"

static const char *TAG = "SAFETY";
static system_state_t state = SYSTEM_NORMAL;
static int64_t unsafe_since_us;

static void transition(system_state_t next)
{
    if (state != next)
    {
        ESP_LOGI(TAG, "state %d -> %d", state, next);
        state = next;
    }
}

esp_err_t safety_controller_init(void)
{
    state = SYSTEM_NORMAL;
    unsafe_since_us = 0;
    relay_all_off();
    buzzer_off();
    return ESP_OK;
}

system_state_t safety_controller_get_state(void)
{
    return state;
}

void safety_controller_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        sensor_data_t sensors = sensor_manager_get_latest();
        bool stale = sensor_manager_is_stale(&sensors);
        bool valid = sensors.presence_valid && !stale;
        bool unsafe = valid && sensors.presence_detected;
        int64_t now = esp_timer_get_time();

        if (!valid)
        {
            unsafe_since_us = 0;
            buzzer_off();
            relay_all_off();
            transition(SYSTEM_FAULT);
        }
        else if (!unsafe)
        {
            unsafe_since_us = 0;
            buzzer_off();
            transition(SYSTEM_NORMAL);
        }
        else
        {
            if (unsafe_since_us == 0)
            {
                unsafe_since_us = now;
                transition(SYSTEM_WARNING);
            }
            int64_t elapsed_ms = (now - unsafe_since_us) / 1000;
            if (elapsed_ms >= (int64_t)SHUTDOWN_DELAY_MS)
            {
                buzzer_on();
                relay_all_off();
                transition(SYSTEM_SHUTDOWN);
            }
            else if (elapsed_ms >= (int64_t)WARNING_DELAY_MS)
            {
                buzzer_on();
                transition(SYSTEM_SHUTDOWN_PENDING);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}