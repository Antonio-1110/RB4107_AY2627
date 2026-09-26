/*
 * TODO section 6: ESP32-S3 central controller base project
 * (Waveshare ESP32-S3-ETH-8DI-8RO, ESP-IDF + FreeRTOS).
 *
 * Board bring-up: prints chip information and the Wi-Fi STA MAC (the sensor
 * node needs it as its ESP-NOW peer), scans the board I2C bus for the TCA9554
 * relay expander and the PCF85063 RTC, then logs a periodic health line.
 */
#include <inttypes.h>
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rb_board_s3.h"
#include "rb_config.h"

static const char *TAG = "CONTROLLER";

void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));

    ESP_LOGI(TAG, "RB4107 controller (ESP32-S3 rev v%d.%d, %d cores, flash %" PRIu32 " MB, IDF %s)",
             chip.revision / 100, chip.revision % 100, chip.cores, flash_size / (1024 * 1024), esp_get_idf_version());
    ESP_LOGI(TAG, "Wi-Fi STA MAC " MACSTR "  <- set this as RB_NODE_CONTROLLER_MAC on the sensor node", MAC2STR(mac));
    ESP_LOGI(TAG, "reset reason %d", esp_reset_reason());

    rb_board_i2c_scan();

    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(CONFIG_RB_S3_HEALTH_LOG_PERIOD_MS));
        ESP_LOGI(TAG, "uptime %" PRIu64 " s, free heap %" PRIu32 " B (min %" PRIu32 " B)",
                 (uint64_t)(esp_timer_get_time() / 1000000), esp_get_free_heap_size(),
                 esp_get_minimum_free_heap_size());
    }
}
