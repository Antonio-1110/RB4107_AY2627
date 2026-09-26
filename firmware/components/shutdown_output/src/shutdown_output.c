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

static esp_err_t set(bool shutdown)
{
    ESP_RETURN_ON_FALSE(s_expander != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_ERROR(tca9554_set_pin(s_expander, s_cfg.channel - 1, pin_level(shutdown)), TAG, "relay write");
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
    s_cfg = *config;
    s_active = config->boot_active;

    /* Every relay de-energised, except the shutdown relay in its boot state. */
    const uint8_t off = s_cfg.relay_active_high ? 0x00 : 0xFF;
    const uint8_t bit = (uint8_t)(1u << (s_cfg.channel - 1));
    const uint8_t initial = pin_level(s_active) ? (uint8_t)(off | bit) : (uint8_t)(off & ~bit);

    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(rb_board_i2c_bus(&bus), TAG, "I2C bus");
    ESP_RETURN_ON_ERROR(tca9554_create(bus, CONFIG_RB_S3_TCA9554_ADDRESS, initial, 0xFF, &s_expander), TAG,
                        "relay expander");
    ESP_LOGI(TAG, "shutdown relay %u ready: %s to shut down, boot state %s", s_cfg.channel,
             s_cfg.energise_to_shut_down ? "energise" : "de-energise", s_active ? "ACTIVE" : "released");
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
    const uint8_t bit = (uint8_t)(1u << (s_cfg.channel - 1));
    const bool expected = pin_level(s_active);
    if ((config & bit) != 0 || ((output & bit) != 0) != expected) {
        /* Expander reset (e.g. brownout) or corrupted write: restore it, but still report the fault. */
        ESP_LOGE(TAG, "relay %u read-back mismatch (output 0x%02x config 0x%02x); restoring", s_cfg.channel, output,
                 config);
        tca9554_restore(s_expander);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}
