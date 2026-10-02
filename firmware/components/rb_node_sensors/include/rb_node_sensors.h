#pragma once

#include "c4002.h"
#include "mlx90640.h"
#include "thermal_features.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* C4002 UART settings and detection settings from menuconfig. */
c4002_config_t rb_node_c4002_config(void);
c4002_settings_t rb_node_c4002_defaults(void);

/*
 * Initialise the C4002 UART and push the detection settings: the ones saved
 * on this node by remote tuning if there are any, else the menuconfig ones
 * (if RB_C4002_APPLY_SETTINGS). Starts an environment calibration if enabled
 * in menuconfig. A missing sensor is logged and reported through the return
 * value; the reading simply stays invalid.
 */
esp_err_t rb_node_c4002_start(void);

/* The detection settings in use, and whether they are saved in NVS. */
c4002_settings_t rb_node_c4002_settings(void);
bool rb_node_c4002_settings_saved(void);
void rb_node_c4002_set_current(const c4002_settings_t *settings, bool saved);

/* Settings saved in NVS by remote tuning (survive reboots and reflashing the app). */
bool rb_node_c4002_load_saved(c4002_settings_t *out);
esp_err_t rb_node_c4002_save(const c4002_settings_t *settings);
esp_err_t rb_node_c4002_forget(void);

/* Create the node I2C bus and initialise the MLX90640 on it. */
esp_err_t rb_node_thermal_start(void);

/* Feature-extraction thresholds from menuconfig. */
thermal_features_config_t rb_node_thermal_features_config(void);

#ifdef __cplusplus
}
#endif
