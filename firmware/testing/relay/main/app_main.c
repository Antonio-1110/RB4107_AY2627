/*
 * TODO section 14: relay / shutdown driver.
 *
 * Puts the relays into the configured safe boot state, then every 5 s
 * alternates shutdown_activate() and shutdown_release(), reading the TCA9554
 * back each time. Listen for the relay click and measure the contacts to
 * confirm the polarity.
 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rb_board_s3.h"
#include "shutdown_output.h"

static const char *TAG = "OUTPUT";

#define TOGGLE_MS 5000

void app_main(void)
{
    rb_board_i2c_scan();
    const shutdown_output_config_t cfg = shutdown_output_config_from_kconfig();
    esp_err_t err = shutdown_output_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "relay expander not usable (%s); check I2C wiring/address", esp_err_to_name(err));
        return;
    }

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(TOGGLE_MS));
        err = shutdown_is_active() ? shutdown_release() : shutdown_activate();
        if (err == ESP_OK) {
            err = shutdown_verify();
        }
        ESP_LOGI(TAG, "shutdown %s, read-back %s", shutdown_is_active() ? "ACTIVE" : "released",
                 err == ESP_OK ? "OK" : esp_err_to_name(err));
    }
}
