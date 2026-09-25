#include "sensor_manager.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "system_config.h"

static const char *TAG = "SENSORS";
static sensor_data_t latest;
static SemaphoreHandle_t lock;

esp_err_t sensor_manager_init(void)
{
    lock = xSemaphoreCreateMutex();
    if (lock == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    latest.timestamp_us = 0;
    ESP_LOGI(TAG, "sensor manager ready; C6 transport is modular and not configured");
    return ESP_OK;
}

sensor_data_t sensor_manager_get_latest(void)
{
    sensor_data_t copy = {0};
    if (lock != NULL && xSemaphoreTake(lock, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        copy = latest;
        xSemaphoreGive(lock);
    }
    return copy;
}

bool sensor_manager_is_stale(const sensor_data_t *data)
{
    if (data == NULL || data->timestamp_us <= 0)
    {
        return true;
    }
    return esp_timer_get_time() - data->timestamp_us > (int64_t)SENSOR_TIMEOUT_MS * 1000;
}

esp_err_t sensor_manager_update(const sensor_data_t *data)
{
    if (data == NULL || lock == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(lock, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }
    latest = *data;
    if (latest.timestamp_us <= 0)
    {
        latest.timestamp_us = esp_timer_get_time();
    }
    xSemaphoreGive(lock);
    return ESP_OK;
}