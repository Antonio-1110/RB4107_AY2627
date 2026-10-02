#include "rb_node_sensors.h"

#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "rb_config.h"
#include "rb_protocol.h"

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

c4002_settings_t rb_node_c4002_defaults(void)
{
    c4002_settings_t s = {
        /* Used when RB_C4002_APPLY_SETTINGS is off: the sensor's own defaults (DFRobot docs). */
        .report_period_ds = 5,
        .range_max_cm = 1100,
        .motion_sensitivity = RB_C4002_SENS_MID,
        .presence_sensitivity = RB_C4002_SENS_MID,
        .disappear_delay_s = 1,
        .lock_time_ds = 10,
        .motion_gate_mask = RB_C4002_ALL_GATES,
        .presence_gate_mask = RB_C4002_ALL_GATES,
    };
#if CONFIG_RB_C4002_APPLY_SETTINGS
    s.report_period_ds = CONFIG_RB_C4002_REPORT_PERIOD_DS;
    s.range_min_cm = CONFIG_RB_C4002_RANGE_MIN_CM;
    s.range_max_cm = CONFIG_RB_C4002_RANGE_MAX_CM;
#if CONFIG_RB_C4002_RESOLUTION_20CM
    s.resolution = RB_C4002_RES_20CM;
#else
    s.resolution = RB_C4002_RES_80CM;
#endif
    s.motion_sensitivity = CONFIG_RB_C4002_MOTION_SENSITIVITY;
    s.presence_sensitivity = CONFIG_RB_C4002_PRESENCE_SENSITIVITY;
    s.disappear_delay_s = CONFIG_RB_C4002_DISAPPEAR_DELAY_S;
    s.lock_time_ds = CONFIG_RB_C4002_LOCK_TIME_DS;
#endif
    return s;
}

/* ---- settings saved by remote tuning (NVS) ---- */

#define NVS_NAMESPACE "rb_c4002"
#define NVS_KEY "params"

typedef struct {
    uint8_t version;              /* RB_C4002_PARAMS_VERSION when written */
    c4002_settings_t settings;
} saved_blob_t;

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init(); /* also done by Wi-Fi start; a second call is harmless */
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase");
        err = nvs_flash_init();
    }
    return err;
}

bool rb_node_c4002_load_saved(c4002_settings_t *out)
{
    nvs_handle_t h;
    if (init_nvs() != ESP_OK || nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    saved_blob_t blob;
    size_t len = sizeof(blob);
    const esp_err_t err = nvs_get_blob(h, NVS_KEY, &blob, &len);
    nvs_close(h);
    if (err != ESP_OK) {
        return false;
    }
    const char *why = NULL;
    if (len != sizeof(blob) || blob.version != RB_C4002_PARAMS_VERSION ||
        (why = rb_c4002_params_check(&blob.settings)) != NULL) {
        ESP_LOGW(TAG, "ignoring saved C4002 settings from older firmware or corrupt (%s)", why ? why : "layout");
        return false;
    }
    *out = blob.settings;
    return true;
}

esp_err_t rb_node_c4002_save(const c4002_settings_t *settings)
{
    ESP_RETURN_ON_ERROR(init_nvs(), TAG, "nvs");
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h), TAG, "nvs open");
    const saved_blob_t blob = {.version = RB_C4002_PARAMS_VERSION, .settings = *settings};
    esp_err_t err = nvs_set_blob(h, NVS_KEY, &blob, sizeof(blob));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t rb_node_c4002_forget(void)
{
    ESP_RETURN_ON_ERROR(init_nvs(), TAG, "nvs");
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h), TAG, "nvs open");
    esp_err_t err = nvs_erase_key(h, NVS_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

/* ---- start-up ---- */

static c4002_settings_t s_current;
static bool s_saved;

c4002_settings_t rb_node_c4002_settings(void)
{
    return s_current;
}

bool rb_node_c4002_settings_saved(void)
{
    return s_saved;
}

void rb_node_c4002_set_current(const c4002_settings_t *settings, bool saved)
{
    s_current = *settings;
    s_saved = saved;
}

esp_err_t rb_node_c4002_start(void)
{
    s_current = rb_node_c4002_defaults();
    s_saved = rb_node_c4002_load_saved(&s_current);
    const c4002_config_t config = rb_node_c4002_config();
    ESP_RETURN_ON_ERROR(c4002_init(&config), TAG, "C4002 UART init failed");

    /* Settings saved from the dashboard always win; otherwise menuconfig decides. */
    if (s_saved || CONFIG_RB_C4002_APPLY_SETTINGS) {
        ESP_LOGI(TAG, "C4002 settings from %s", s_saved ? "the dashboard (saved on this node)" : "menuconfig");
        esp_err_t err = c4002_apply_settings(&s_current);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "C4002 did not accept settings (%s); check wiring, pins and baud rate", esp_err_to_name(err));
            return err;
        }
    }
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
