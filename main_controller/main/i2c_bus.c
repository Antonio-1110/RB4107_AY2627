#include "i2c_bus.h"

#include "esp_check.h"
#include "system_config.h"

static i2c_master_bus_handle_t bus;

esp_err_t board_i2c_init(void)
{
    if (bus != NULL)
    {
        return ESP_OK;
    }
    i2c_master_bus_config_t config = {
        .i2c_port = TCA9554_I2C_PORT,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&config, &bus);
}

i2c_master_bus_handle_t board_i2c_get_bus(void)
{
    return bus;
}