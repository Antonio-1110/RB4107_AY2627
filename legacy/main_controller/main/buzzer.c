#include "buzzer.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "system_config.h"

static const char *TAG = "BUZZER";
static bool buzzer_state;

static uint64_t buzzer_pin_mask(gpio_num_t pin)
{
    return 1ULL << (uint32_t)pin;
}

esp_err_t buzzer_init(void)
{
    if (BUZZER_GPIO == GPIO_NUM_NC)
    {
        ESP_LOGW(TAG, "buzzer GPIO is not configured");
        return ESP_OK;
    }
    gpio_config_t config = {
        .pin_bit_mask = buzzer_pin_mask(BUZZER_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "buzzer GPIO config failed");
    buzzer_off();
    return ESP_OK;
}

void buzzer_on(void)
{
    buzzer_state = true;
    if (BUZZER_GPIO != GPIO_NUM_NC)
    {
        (void)gpio_set_level(BUZZER_GPIO, BUZZER_ACTIVE_LEVEL);
    }
}

void buzzer_off(void)
{
    buzzer_state = false;
    if (BUZZER_GPIO != GPIO_NUM_NC)
    {
        (void)gpio_set_level(BUZZER_GPIO, !BUZZER_ACTIVE_LEVEL);
    }
}

void buzzer_beep(uint32_t duration_ms)
{
    buzzer_on();
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    buzzer_off();
}

bool buzzer_is_on(void)
{
    return buzzer_state;
}