#include "shutdown_output.h"

#include "esp_check.h"
#include "esp_log.h"
#include "rb_board_s3.h"
#include "rb_config.h"
#include "tca9554.h"

static const char *TAG = "OUTPUT";

static shutdown_output_config_t s_cfg;
static tca9554_handle_t s_expander;
static bool s_active;

shutdown_output_config_t shutdown_output_config_from_kconfig(void)
{
    return (shutdown_output_config_t){
        .channel = CONFIG_RB_SHUTDOWN_RELAY_CHANNEL,
        .valve_channel = CONFIG_RB_SHUTDOWN_VALVE_CHANNEL,
        .relay_active_high = CONFIG_RB_RELAY_ACTIVE_LEVEL == 1,
#if CONFIG_RB_SHUTDOWN_ENERGISE_TO_SHUT_DOWN
        .energise_to_shut_down = true,
#else
        .energise_to_shut_down = false,
#endif
#if CONFIG_RB_SHUTDOWN_BOOT_ACTIVE
        .boot_active = true,
#else
        .boot_active = false,
#endif
    };
}

/* Expander pin level for a requested shutdown state. */
static bool pin_level(bool shutdown)
{
    const bool energise = shutdown == s_cfg.energise_to_shut_down;
    return energise == s_cfg.relay_active_high;
}

/*
 * Expander pin level of the wired valve line: ON (sinking) only while it is
 * safe to keep the gas valve open, whatever the relay polarity, so a
 * controller with no power also closes the valve.
 */
static bool valve_level(bool shutdown)
{
    return !shutdown == s_cfg.relay_active_high;
}

static uint8_t bit_of(uint8_t channel)
{
    return channel == 0 ? 0 : (uint8_t)(1u << (channel - 1));
}

static esp_err_t set(bool shutdown)
{
    ESP_RETURN_ON_FALSE(s_expander != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_ERROR(tca9554_set_pin(s_expander, s_cfg.channel - 1, pin_level(shutdown)), TAG, "relay write");
    if (s_cfg.valve_channel != 0) {
        ESP_RETURN_ON_ERROR(tca9554_set_pin(s_expander, s_cfg.valve_channel - 1, valve_level(shutdown)), TAG,
                            "valve line write");
    }
    if (shutdown != s_active) {
        if (shutdown) {
            ESP_LOGW(TAG, "shutdown relay %u ACTIVATED", s_cfg.channel);
        } else {
            ESP_LOGI(TAG, "shutdown relay %u released", s_cfg.channel);
        }
    }
    s_active = shutdown;
    return ESP_OK;
}

esp_err_t shutdown_output_init(const shutdown_output_config_t *config)
{
    ESP_RETURN_ON_FALSE(config->channel >= 1 && config->channel <= 8, ESP_ERR_INVALID_ARG, TAG, "channel 1-8");
    ESP_RETURN_ON_FALSE(config->valve_channel <= 8 && config->valve_channel != config->channel, ESP_ERR_INVALID_ARG,
                        TAG, "valve channel must be 0 (none) or 1-8 and differ from the relay channel");
    s_cfg = *config;
    s_active = config->boot_active;

    /* Every relay de-energised, except the shutdown relay and valve line in their boot state. */
    const uint8_t off = s_cfg.relay_active_high ? 0x00 : 0xFF;
    const uint8_t bit = bit_of(s_cfg.channel);
    const uint8_t valve_bit = bit_of(s_cfg.valve_channel);
    uint8_t initial = pin_level(s_active) ? (uint8_t)(off | bit) : (uint8_t)(off & ~bit);
    initial = valve_level(s_active) ? (uint8_t)(initial | valve_bit) : (uint8_t)(initial & ~valve_bit);

    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(rb_board_i2c_bus(&bus), TAG, "I2C bus");
    ESP_RETURN_ON_ERROR(tca9554_create(bus, CONFIG_RB_S3_TCA9554_ADDRESS, initial, 0xFF, &s_expander), TAG,
                        "relay expander");
    ESP_LOGI(TAG, "shutdown relay %u ready: %s to shut down, boot state %s", s_cfg.channel,
             s_cfg.energise_to_shut_down ? "energise" : "de-energise", s_active ? "ACTIVE" : "released");
    if (s_cfg.valve_channel != 0) {
        ESP_LOGI(TAG, "wired valve line on output %u: ON while safe, OFF on shutdown", s_cfg.valve_channel);
    }
    return shutdown_verify();
}

esp_err_t shutdown_activate(void)
{
    return s_active ? ESP_OK : set(true);
}

esp_err_t shutdown_release(void)
{
    return s_active ? set(false) : ESP_OK;
}

bool shutdown_is_active(void)
{
    return s_active;
}

esp_err_t shutdown_verify(void)
{
    uint8_t output, config;
    ESP_RETURN_ON_ERROR(tca9554_read_back(s_expander, &output, &config), TAG, "read back");
    const uint8_t bit = bit_of(s_cfg.channel);
    const uint8_t valve_bit = bit_of(s_cfg.valve_channel);
    const bool valve_ok =
        valve_bit == 0 || ((config & valve_bit) == 0 && ((output & valve_bit) != 0) == valve_level(s_active));
    if ((config & bit) != 0 || ((output & bit) != 0) != pin_level(s_active) || !valve_ok) {
        /* Expander reset (e.g. brownout) or corrupted write: restore it, but still report the fault. */
        ESP_LOGE(TAG, "relay %u read-back mismatch (output 0x%02x config 0x%02x); restoring", s_cfg.channel, output,
                 config);
        tca9554_restore(s_expander);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}
