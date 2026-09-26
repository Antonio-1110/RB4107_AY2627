/*
 * TODO section 29: end-to-end integration, controller side.
 *
 *   C4002 + MLX90640 -> ESP32-C6 (project 05) -> ESP-NOW -> this ESP32-S3
 *     -> safety state machine -> buzzer + shutdown relay
 *     -> Ethernet -> Mosquitto (MacBook) -> Django subscriber
 *
 * This is the production S3 firmware and also covers:
 *   - section 27 (diagnostic mode): serial console, plus the simulated node
 *     with sdkconfig.qemu or RB_SIM_NODE,
 *   - section 30 (critical failure test): continuity monitor
 *     (RB_DIAG_CONTINUITY_MONITOR).
 *
 * Every component is wired in components/rb_controller_app; the test
 * procedures are in README.md.
 */
#include "esp_err.h"
#include "rb_controller_app.h"

void app_main(void)
{
    ESP_ERROR_CHECK(rb_controller_app_start());
}
