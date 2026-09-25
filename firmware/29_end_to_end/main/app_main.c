/*
 * TODO section 29: end-to-end integration, controller side.
 *
 *   C4002 + MLX90640 -> ESP32-C6 (project 05) -> ESP-NOW -> this ESP32-S3
 *     -> safety state machine -> buzzer + shutdown relay
 *     -> Ethernet -> Mosquitto (MacBook) -> Django subscriber
 *
 * Every component is wired in components/rb_controller_app; the test
 * procedure is in README.md.
 */
#include "esp_err.h"
#include "rb_controller_app.h"

void app_main(void)
{
    ESP_ERROR_CHECK(rb_controller_app_start());
}
