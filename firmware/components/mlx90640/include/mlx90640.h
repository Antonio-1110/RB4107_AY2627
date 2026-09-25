#pragma once

/*
 * MLX90640 32x24 thermal sensor on the ESP-IDF I2C master driver.
 *
 * Wraps the official Melexis driver: reads the calibration EEPROM once, then
 * mlx90640_read_frame() gathers both sub-pages (chess pattern) into one
 * complete 768-pixel frame in degrees Celsius.
 */
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MLX90640_COLS 32
#define MLX90640_ROWS 24
#define MLX90640_PIXELS (MLX90640_COLS * MLX90640_ROWS)
#define MLX90640_DEFAULT_ADDR 0x33

/* Sub-page refresh rate (a full frame needs two sub-pages). */
typedef enum {
    MLX90640_REFRESH_1HZ = 0x02,
    MLX90640_REFRESH_2HZ = 0x03,
    MLX90640_REFRESH_4HZ = 0x04,
    MLX90640_REFRESH_8HZ = 0x05,
    MLX90640_REFRESH_16HZ = 0x06,
} mlx90640_refresh_t;

typedef struct {
    i2c_master_bus_handle_t bus;
    uint8_t address;
    uint32_t scl_hz;
    mlx90640_refresh_t refresh;
    float emissivity;
} mlx90640_config_t;

esp_err_t mlx90640_init(const mlx90640_config_t *config);

/*
 * Read one complete frame. to_c receives 768 temperatures, row-major
 * (index = row * 32 + col). ta_c (optional) receives the sensor's ambient
 * temperature. Returns ESP_ERR_TIMEOUT if a sub-page is not ready in time
 * and ESP_FAIL on I2C or frame-data errors.
 */
esp_err_t mlx90640_read_frame(float to_c[MLX90640_PIXELS], float *ta_c, uint32_t timeout_ms);

/* Last Melexis driver error code (negative), 0 if none. For diagnostics. */
int mlx90640_last_error(void);

#ifdef __cplusplus
}
#endif
