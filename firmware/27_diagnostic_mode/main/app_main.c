/*
 * TODO section 27: diagnostic mode.
 *
 * The complete controller with the diagnostic console and, by default, the
 * simulated sensor node (so it runs on a bare S3 or in QEMU). Example session:
 *
 *   rb4107> timers test          warning 5 s, shutdown 10 s
 *   rb4107> sim temp 120         heating -> MONITORING
 *   rb4107> sim absent           -> UNATTENDED -> WARNING -> SHUTDOWN
 *   rb4107> status
 *   rb4107> sim present
 *   rb4107> reset                SHUTDOWN -> IDLE -> MONITORING
 *   rb4107> sim thermal-invalid  -> FAULT
 */
#include "esp_err.h"
#include "rb_controller_app.h"

void app_main(void)
{
    ESP_ERROR_CHECK(rb_controller_app_start());
}
