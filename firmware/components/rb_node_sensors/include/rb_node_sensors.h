#pragma once

#include "c4002.h"
#include "mlx90640.h"
#include "thermal_features.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* C4002 settings built from menuconfig. */
c4002_config_t rb_node_c4002_config(void);
c4002_settings_t rb_node_c4002_settings(void);

/*
 * Initialise the C4002 UART and, if enabled in menuconfig, push the detection
 * settings and start an environment calibration. A missing sensor is logged
 * and reported through the return value; the reading simply stays invalid.
 */
esp_err_t rb_node_c4002_start(void);

/* Create the node I2C bus and initialise the MLX90640 on it. */
esp_err_t rb_node_thermal_start(void);

/* Feature-extraction thresholds from menuconfig. */
thermal_features_config_t rb_node_thermal_features_config(void);

#ifdef __cplusplus
}
#endif
