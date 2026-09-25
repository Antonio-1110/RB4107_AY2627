#include "rtc.h"

#include <sys/time.h>
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "i2c_bus.h"
#include "system_config.h"

static const char *TAG = "RTC";
static i2c_master_dev_handle_t rtc_device;

static uint8_t from_bcd(uint8_t value)
{
    return (uint8_t)((value >> 4U) * 10U + (value & 0x0fU));
}

static uint8_t to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10U) << 4U) | (value % 10U));
}

static esp_err_t rtc_write_time(const struct tm *time_value)
{
    uint8_t data[] = {
        0x04,
        to_bcd((uint8_t)time_value->tm_sec),
        to_bcd((uint8_t)time_value->tm_min),
        to_bcd((uint8_t)time_value->tm_hour),
        to_bcd((uint8_t)time_value->tm_mday),
        to_bcd((uint8_t)time_value->tm_wday),
        to_bcd((uint8_t)(time_value->tm_mon + 1)),
        to_bcd((uint8_t)(time_value->tm_year - 100)),
    };
    return i2c_master_transmit(rtc_device, data, sizeof(data), 100);
}

static esp_err_t rtc_read_time(struct tm *time_value)
{
    uint8_t reg = 0x04;
    uint8_t data[7];
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(rtc_device, &reg, 1, data, sizeof(data), 100), TAG, "RTC read failed");
    if ((data[0] & 0x80U) != 0U)
    {
        return ESP_ERR_INVALID_STATE;
    }
    *time_value = (struct tm){
        .tm_sec = from_bcd(data[0] & 0x7fU),
        .tm_min = from_bcd(data[1] & 0x7fU),
        .tm_hour = from_bcd(data[2] & 0x3fU),
        .tm_mday = from_bcd(data[3] & 0x3fU),
        .tm_wday = from_bcd(data[4] & 0x07U),
        .tm_mon = from_bcd(data[5] & 0x1fU) - 1,
        .tm_year = 100 + from_bcd(data[6]),
    };
    return ESP_OK;
}

esp_err_t app_rtc_init(void)
{
    ESP_RETURN_ON_ERROR(board_i2c_init(), TAG, "I2C bus init failed");
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF85063_I2C_ADDRESS,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(board_i2c_get_bus(), &config, &rtc_device), TAG, "RTC device init failed");
    struct tm current_time;
    if (rtc_read_time(&current_time) == ESP_OK)
    {
        time_t timestamp = mktime(&current_time);
        ESP_RETURN_ON_ERROR(rtc_set_time(timestamp), TAG, "system clock restore failed");
        ESP_LOGI(TAG, "restored time from PCF85063");
    }
    else
    {
        ESP_LOGW(TAG, "RTC time invalid or unavailable; waiting for network time");
    }
    return ESP_OK;
}

time_t rtc_get_timestamp(void)
{
    return time(NULL);
}

esp_err_t rtc_set_time(time_t timestamp)
{
    struct timeval value = {.tv_sec = timestamp, .tv_usec = 0};
    if (settimeofday(&value, NULL) != 0)
    {
        return ESP_FAIL;
    }
    struct tm time_value;
    localtime_r(&timestamp, &time_value);
    return rtc_write_time(&time_value);
}