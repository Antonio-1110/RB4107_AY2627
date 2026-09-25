#include "tca9554.h"

#include <stdlib.h>
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "TCA9554";

#define REG_INPUT 0x00
#define REG_OUTPUT 0x01
#define REG_POLARITY 0x02
#define REG_CONFIG 0x03 /* 1 = input (power-on default 0xFF) */
#define I2C_TIMEOUT_MS 50

struct tca9554 {
    i2c_master_dev_handle_t dev;
    uint8_t output;
    uint8_t config;
};

static esp_err_t write_reg(tca9554_handle_t t, uint8_t reg, uint8_t value)
{
    const uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(t->dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

static esp_err_t read_reg(tca9554_handle_t t, uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(t->dev, &reg, 1, value, 1, I2C_TIMEOUT_MS);
}

esp_err_t tca9554_create(i2c_master_bus_handle_t bus, uint8_t address, uint8_t initial_output, uint8_t output_mask,
                         tca9554_handle_t *out)
{
    tca9554_handle_t t = calloc(1, sizeof(*t));
    ESP_RETURN_ON_FALSE(t != NULL, ESP_ERR_NO_MEM, TAG, "no memory");
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 100000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &t->dev);
    if (err == ESP_OK) {
        t->output = initial_output;
        t->config = (uint8_t)~output_mask;
        /* Order matters: output value first, then direction. */
        err = write_reg(t, REG_OUTPUT, t->output);
        if (err == ESP_OK) {
            err = write_reg(t, REG_POLARITY, 0x00);
        }
        if (err == ESP_OK) {
            err = write_reg(t, REG_CONFIG, t->config);
        }
    }
    if (err != ESP_OK) {
        if (t->dev != NULL) {
            i2c_master_bus_rm_device(t->dev);
        }
        free(t);
        ESP_LOGE(TAG, "no TCA9554 at 0x%02x: %s", address, esp_err_to_name(err));
        return err;
    }
    *out = t;
    return ESP_OK;
}

esp_err_t tca9554_set_pin(tca9554_handle_t t, uint8_t pin, bool level)
{
    ESP_RETURN_ON_FALSE(t != NULL && pin < 8, ESP_ERR_INVALID_ARG, TAG, "bad pin");
    const uint8_t next = level ? (uint8_t)(t->output | (1u << pin)) : (uint8_t)(t->output & ~(1u << pin));
    ESP_RETURN_ON_ERROR(write_reg(t, REG_OUTPUT, next), TAG, "write");
    t->output = next;
    return ESP_OK;
}

esp_err_t tca9554_read_back(tca9554_handle_t t, uint8_t *output, uint8_t *config)
{
    ESP_RETURN_ON_FALSE(t != NULL, ESP_ERR_INVALID_ARG, TAG, "no device");
    ESP_RETURN_ON_ERROR(read_reg(t, REG_OUTPUT, output), TAG, "read output");
    return read_reg(t, REG_CONFIG, config);
}

esp_err_t tca9554_restore(tca9554_handle_t t)
{
    ESP_RETURN_ON_FALSE(t != NULL, ESP_ERR_INVALID_ARG, TAG, "no device");
    ESP_RETURN_ON_ERROR(write_reg(t, REG_OUTPUT, t->output), TAG, "restore output");
    return write_reg(t, REG_CONFIG, t->config);
}

uint8_t tca9554_cached_output(tca9554_handle_t t)
{
    return t != NULL ? t->output : 0;
}
