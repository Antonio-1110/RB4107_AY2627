#include "rb_node_sensors.h"

#include "esp_check.h"
#include "esp_log.h"
#include "rb_config.h"

static const char *TAG = "SENSOR";

c4002_config_t rb_node_c4002_config(void)
{
    return (c4002_config_t){
        .uart_port = CONFIG_RB_C4002_UART_PORT,
        .rx_gpio = CONFIG_RB_C4002_RX_GPIO,
        .tx_gpio = CONFIG_RB_C4002_TX_GPIO,
        .out_gpio = CONFIG_RB_C4002_OUT_GPIO,
        .baud = CONFIG_RB_C4002_BAUD,
        .stale_timeout_ms = CONFIG_RB_C4002_STALE_TIMEOUT_MS,
    };
}

c4002_settings_t rb_node_c4002_settings(void)
{
    c4002_settings_t s = {0};
#if CONFIG_RB_C4002_APPLY_SETTINGS
    s.report_period_ds = CONFIG_RB_C4002_REPORT_PERIOD_DS;
    s.range_min_cm = CONFIG_RB_C4002_RANGE_MIN_CM;
    s.range_max_cm = CONFIG_RB_C4002_RANGE_MAX_CM;
#if CONFIG_RB_C4002_RESOLUTION_20CM
    s.resolution = C4002_RESOLUTION_20CM;
#else
    s.resolution = C4002_RESOLUTION_80CM;
#endif
    s.motion_sensitivity = (c4002_sensitivity_t)CONFIG_RB_C4002_MOTION_SENSITIVITY;
    s.presence_sensitivity = (c4002_sensitivity_t)CONFIG_RB_C4002_PRESENCE_SENSITIVITY;
    s.disappear_delay_s = CONFIG_RB_C4002_DISAPPEAR_DELAY_S;
    s.run_led = true;
    s.out_led = true;
#endif
    return s;
}

esp_err_t rb_node_c4002_start(void)
{
    const c4002_config_t config = rb_node_c4002_config();
    ESP_RETURN_ON_ERROR(c4002_init(&config), TAG, "C4002 UART init failed");

#if CONFIG_RB_C4002_APPLY_SETTINGS
    const c4002_settings_t settings = rb_node_c4002_settings();
    esp_err_t err = c4002_apply_settings(&settings);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "C4002 did not accept settings (%s); check wiring, pins and baud rate", esp_err_to_name(err));
        return err;
    }
#endif
#if CONFIG_RB_C4002_ENV_CALIBRATION_AT_BOOT
    ESP_LOGW(TAG, "C4002 environment calibration for %d s: keep the area empty", CONFIG_RB_C4002_ENV_CALIBRATION_S);
    ESP_RETURN_ON_ERROR(c4002_start_env_calibration(5, CONFIG_RB_C4002_ENV_CALIBRATION_S), TAG, "calibration");
#endif
    return ESP_OK;
}
