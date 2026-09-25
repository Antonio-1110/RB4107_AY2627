#include "relay_control.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "i2c_bus.h"
#include "system_config.h"

static const char *TAG = "RELAY";
static bool relay_states[RELAY_COUNT];
static uint8_t relay_output;
static i2c_master_dev_handle_t tca9554;

static esp_err_t tca9554_write(uint8_t reg, uint8_t value)
{
    uint8_t command[] = {reg, value};
    return i2c_master_transmit(tca9554, command, sizeof(command), 100);
}

static esp_err_t relay_write_output(void)
{
    return tca9554_write(0x01, relay_output);
}

esp_err_t relay_init(void)
{
    ESP_RETURN_ON_ERROR(board_i2c_init(), TAG, "I2C bus init failed");

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TCA9554_I2C_ADDRESS,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(board_i2c_get_bus(), &device_config, &tca9554), TAG, "TCA9554 init failed");
    relay_output = 0;
    ESP_RETURN_ON_ERROR(tca9554_write(0x03, 0x00), TAG, "TCA9554 direction failed");
    ESP_RETURN_ON_ERROR(relay_write_output(), TAG, "TCA9554 safe output failed");
    for (uint8_t index = 0; index < RELAY_COUNT; ++index)
    {
        relay_states[index] = false;
    }
    ESP_LOGI(TAG, "TCA9554 relay controller ready at 0x%02x", TCA9554_I2C_ADDRESS);
    return ESP_OK;
}

esp_err_t relay_set(uint8_t relay, bool state)
{
    if (relay == 0 || relay > RELAY_COUNT || tca9554 == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t bit = 1U << (relay - 1U);
    if (state)
    {
        relay_output = (relay_output & (uint8_t)~bit) | (TCA9554_ACTIVE_LEVEL ? bit : 0);
    }
    else
    {
        relay_output = (relay_output & (uint8_t)~bit) | (TCA9554_ACTIVE_LEVEL ? 0 : bit);
    }
    ESP_RETURN_ON_ERROR(relay_write_output(), TAG, "relay write failed");
    relay_states[relay - 1U] = state;
    return ESP_OK;
}

bool relay_get(uint8_t relay)
{
    return relay > 0 && relay <= RELAY_COUNT ? relay_states[relay - 1] : false;
}

void relay_all_off(void)
{
    for (uint8_t relay = 1; relay <= RELAY_COUNT; ++relay)
    {
        (void)relay_set(relay, false);
    }
}