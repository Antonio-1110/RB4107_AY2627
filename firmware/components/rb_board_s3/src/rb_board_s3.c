#include "rb_board_s3.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "rb_config.h"

static const char *TAG = "BOARD";
static i2c_master_bus_handle_t s_bus;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

esp_err_t rb_board_i2c_bus(i2c_master_bus_handle_t *out)
{
    portENTER_CRITICAL(&s_lock);
    i2c_master_bus_handle_t bus = s_bus;
    portEXIT_CRITICAL(&s_lock);
    if (bus == NULL) {
        const i2c_master_bus_config_t cfg = {
            .i2c_port = CONFIG_RB_S3_I2C_PORT,
            .sda_io_num = CONFIG_RB_S3_I2C_SDA_GPIO,
            .scl_io_num = CONFIG_RB_S3_I2C_SCL_GPIO,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        esp_err_t err = i2c_new_master_bus(&cfg, &bus);
        if (err == ESP_ERR_INVALID_STATE) {
            /* Someone else created it concurrently. */
            err = i2c_master_get_bus_handle(CONFIG_RB_S3_I2C_PORT, &bus);
        }
        ESP_RETURN_ON_ERROR(err, TAG, "I2C bus");
        portENTER_CRITICAL(&s_lock);
        s_bus = bus;
        portEXIT_CRITICAL(&s_lock);
    }
    *out = bus;
    return ESP_OK;
}

int rb_board_i2c_scan(void)
{
    i2c_master_bus_handle_t bus;
    if (rb_board_i2c_bus(&bus) != ESP_OK) {
        return 0;
    }
    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (i2c_master_probe(bus, addr, 20) == ESP_OK) {
            const char *name = addr == CONFIG_RB_S3_TCA9554_ADDRESS    ? " (TCA9554 relay expander)"
                               : addr == CONFIG_RB_S3_PCF85063_ADDRESS ? " (PCF85063 RTC)"
                                                                       : "";
            ESP_LOGI(TAG, "I2C device at 0x%02x%s", addr, name);
            found++;
        }
    }
    ESP_LOGI(TAG, "I2C scan: %d device(s) on SDA=%d SCL=%d", found, CONFIG_RB_S3_I2C_SDA_GPIO,
             CONFIG_RB_S3_I2C_SCL_GPIO);
    return found;
}
