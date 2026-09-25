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

esp_err_t rb_node_thermal_start(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = CONFIG_RB_MLX_I2C_PORT,
        .sda_io_num = CONFIG_RB_MLX_SDA_GPIO,
        .scl_io_num = CONFIG_RB_MLX_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &bus), TAG, "I2C bus init failed");

#if CONFIG_RB_MLX_REFRESH_2HZ
    const mlx90640_refresh_t refresh = MLX90640_REFRESH_2HZ;
#elif CONFIG_RB_MLX_REFRESH_8HZ
    const mlx90640_refresh_t refresh = MLX90640_REFRESH_8HZ;
#else
    const mlx90640_refresh_t refresh = MLX90640_REFRESH_4HZ;
#endif
    const mlx90640_config_t mlx_cfg = {
        .bus = bus,
        .address = CONFIG_RB_MLX_ADDRESS,
        .scl_hz = CONFIG_RB_MLX_I2C_HZ,
        .refresh = refresh,
        .emissivity = CONFIG_RB_MLX_EMISSIVITY_PCT / 100.0f,
    };
    esp_err_t err = mlx90640_init(&mlx_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MLX90640 init failed (%s): check 3.3 V, GND, SDA=%d, SCL=%d", esp_err_to_name(err),
                 CONFIG_RB_MLX_SDA_GPIO, CONFIG_RB_MLX_SCL_GPIO);
    }
    return err;
}

thermal_features_config_t rb_node_thermal_features_config(void)
{
    return (thermal_features_config_t){
        .hot_pixel_threshold_c = CONFIG_RB_THERMAL_HOT_PIXEL_THRESHOLD_DC / 10.0f,
        .hot_region_radius = CONFIG_RB_THERMAL_HOT_REGION_RADIUS,
        .rate_window_ms = CONFIG_RB_THERMAL_RATE_WINDOW_S * 1000u,
        .valid_min_c = CONFIG_RB_THERMAL_VALID_MIN_DC / 10.0f,
        .valid_max_c = CONFIG_RB_THERMAL_VALID_MAX_DC / 10.0f,
        .max_invalid_pixels = CONFIG_RB_THERMAL_MAX_INVALID_PIXELS,
    };
}
