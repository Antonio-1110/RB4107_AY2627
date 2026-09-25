#include "mlx90640.h"

#include "MLX90640_API.h"
#include "MLX90640_I2C_Driver.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mlx90640_i2c_esp.h"

static const char *TAG = "THERMAL";

/* Melexis: reflected temperature = ambient - 8 degC for a sensor in open air. */
#define TA_SHIFT 8.0f
#define STATUS_POLL_MS 5

/* Large buffers (~11 KB total), kept static and off the task stacks. */
static paramsMLX90640 s_params;
static uint16_t s_eeprom[MLX90640_EEPROM_DUMP_NUM];
static uint16_t s_frame[834];
static uint8_t s_addr;
static float s_emissivity;
static int s_last_error;

esp_err_t mlx90640_init(const mlx90640_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL && config->bus != NULL, ESP_ERR_INVALID_ARG, TAG, "no I2C bus");
    s_addr = config->address;
    s_emissivity = config->emissivity;

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->address,
        .scl_speed_hz = config->scl_hz,
    };
    i2c_master_dev_handle_t dev;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(config->bus, &dev_cfg, &dev), TAG, "add device");
    mlx90640_i2c_set_device(dev);

    s_last_error = MLX90640_DumpEE(s_addr, s_eeprom);
    ESP_RETURN_ON_FALSE(s_last_error == 0, ESP_ERR_NOT_FOUND, TAG, "no MLX90640 at 0x%02x (err %d)", s_addr, s_last_error);
    s_last_error = MLX90640_ExtractParameters(s_eeprom, &s_params);
    /* Broken/outlier-pixel errors are still usable; anything else means a bad EEPROM read. */
    if (s_last_error != 0 && s_last_error != -MLX90640_BROKEN_PIXELS_NUM_ERROR &&
        s_last_error != -MLX90640_OUTLIER_PIXELS_NUM_ERROR) {
        ESP_LOGE(TAG, "calibration data invalid (err %d)", s_last_error);
        return ESP_ERR_INVALID_CRC;
    }
    if (s_last_error != 0) {
        ESP_LOGW(TAG, "sensor reports defective pixels (err %d); they will be interpolated", s_last_error);
    }
    ESP_RETURN_ON_FALSE(MLX90640_SetRefreshRate(s_addr, config->refresh) == 0, ESP_FAIL, TAG, "refresh rate");
    ESP_RETURN_ON_FALSE(MLX90640_SetChessMode(s_addr) == 0, ESP_FAIL, TAG, "chess mode");
    ESP_LOGI(TAG, "MLX90640 ready at 0x%02x, refresh code %d, %lu Hz I2C", s_addr, config->refresh,
             (unsigned long)config->scl_hz);
    s_last_error = 0;
    return ESP_OK;
}

static esp_err_t wait_data_ready(int64_t deadline_us)
{
    uint16_t status = 0;
    while (esp_timer_get_time() < deadline_us) {
        if (MLX90640_I2CRead(s_addr, MLX90640_STATUS_REG, 1, &status) != 0) {
            return ESP_FAIL;
        }
        if (MLX90640_GET_DATA_READY(status)) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(STATUS_POLL_MS));
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t mlx90640_read_frame(float to_c[MLX90640_PIXELS], float *ta_c, uint32_t timeout_ms)
{
    const int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    float ta = 0.0f;
    bool got_subpage[2] = {false, false};

    /* Chess mode: each sub-page carries half the pixels; collect both. */
    while (!(got_subpage[0] && got_subpage[1])) {
        esp_err_t err = wait_data_ready(deadline);
        if (err != ESP_OK) {
            return err;
        }
        s_last_error = MLX90640_GetFrameData(s_addr, s_frame);
        if (s_last_error < 0) {
            return ESP_FAIL;
        }
        const int subpage = MLX90640_GetSubPageNumber(s_frame);
        ta = MLX90640_GetTa(s_frame, &s_params);
        MLX90640_CalculateTo(s_frame, &s_params, s_emissivity, ta - TA_SHIFT, to_c);
        got_subpage[subpage & 1] = true;
    }
    MLX90640_BadPixelsCorrection(s_params.brokenPixels, to_c, 1 /* chess */, &s_params);
    MLX90640_BadPixelsCorrection(s_params.outlierPixels, to_c, 1, &s_params);
    s_last_error = 0;
    if (ta_c != NULL) {
        *ta_c = ta;
    }
    return ESP_OK;
}

int mlx90640_last_error(void)
{
    return s_last_error;
}
