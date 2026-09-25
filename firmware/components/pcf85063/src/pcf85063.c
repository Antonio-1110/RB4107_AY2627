#include "pcf85063.h"

#include "esp_check.h"

static const char *TAG = "RTC";

#define REG_SECONDS 0x04
#define OS_FLAG 0x80
#define I2C_TIMEOUT_MS 50

static i2c_master_dev_handle_t s_dev;

static uint8_t from_bcd(uint8_t v)
{
    return (uint8_t)((v >> 4) * 10 + (v & 0x0F));
}

static uint8_t to_bcd(unsigned v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

/* Days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant's algorithm). */
static long days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

esp_err_t pcf85063_init(i2c_master_bus_handle_t bus, uint8_t address)
{
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 100000,
    };
    return i2c_master_bus_add_device(bus, &cfg, &s_dev);
}

esp_err_t pcf85063_read(time_t *utc)
{
    ESP_RETURN_ON_FALSE(s_dev != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    const uint8_t reg = REG_SECONDS;
    uint8_t d[7];
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_dev, &reg, 1, d, sizeof(d), I2C_TIMEOUT_MS), TAG, "read");
    if (d[0] & OS_FLAG) {
        return ESP_ERR_INVALID_STATE;
    }
    const int year = 2000 + from_bcd(d[6]);
    const unsigned month = from_bcd(d[5] & 0x1F);
    const unsigned day = from_bcd(d[3] & 0x3F);
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    const long days = days_from_civil(year, month, day);
    *utc = (time_t)days * 86400 + from_bcd(d[2] & 0x3F) * 3600 + from_bcd(d[1] & 0x7F) * 60 + from_bcd(d[0] & 0x7F);
    return ESP_OK;
}

esp_err_t pcf85063_write(time_t utc)
{
    ESP_RETURN_ON_FALSE(s_dev != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    struct tm t;
    gmtime_r(&utc, &t);
    ESP_RETURN_ON_FALSE(t.tm_year >= 100 && t.tm_year < 200, ESP_ERR_INVALID_ARG, TAG, "year out of range");
    const uint8_t buf[] = {
        REG_SECONDS,
        to_bcd(t.tm_sec) & 0x7F, /* writing seconds clears OS */
        to_bcd(t.tm_min),
        to_bcd(t.tm_hour),
        to_bcd(t.tm_mday),
        to_bcd(t.tm_wday),
        to_bcd(t.tm_mon + 1),
        to_bcd(t.tm_year - 100),
    };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}
