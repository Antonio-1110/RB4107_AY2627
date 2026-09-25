#include "buzzer.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "network.h"
#include "relay_control.h"
#include "rtc.h"
#include "safety_controller.h"
#include "sensor_manager.h"
#include "sensor_node.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client_app.h"

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(app_rtc_init());
    ESP_ERROR_CHECK(relay_init());
    ESP_ERROR_CHECK(buzzer_init());
    ESP_ERROR_CHECK(sensor_manager_init());
    ESP_ERROR_CHECK(safety_controller_init());

    (void)network_init();
    (void)sensor_node_init();
    (void)mqtt_app_init();

    xTaskCreate(safety_controller_task, "safety", 4096, NULL, 10, NULL);
    ESP_LOGI("MAIN", "RB4107 controller started");
}
