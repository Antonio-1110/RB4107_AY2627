/*
 * TODO section 1: basic ESP-IDF project for the FireBeetle 2 ESP32-C6.
 *
 * Checks that the node builds, flashes, logs over USB and stays stable once
 * the sensors are wired: it prints chip, reset reason and MAC information at
 * boot, then a periodic health line (uptime, heap), and blinks the status LED
 * from an esp_timer so nothing blocks.
 */
#include <inttypes.h>
#include "driver/gpio.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "rb_config.h"

static const char *TAG = "NODE";

static const char *reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external pin";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "other watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_USB: return "USB";
    case ESP_RST_JTAG: return "JTAG";
    default: return "unknown";
    }
}

static void log_board_info(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_size = 0;
    (void)esp_flash_get_size(NULL, &flash_size);
    uint8_t mac[6] = {0};
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    esp_reset_reason_t reason = esp_reset_reason();

    ESP_LOGI(TAG, "RB4107 sensor node %d (FireBeetle 2 ESP32-C6)", CONFIG_RB_NODE_ID);
    ESP_LOGI(TAG, "chip rev v%d.%d, %d core(s), flash %" PRIu32 " MB, IDF %s",
             chip.revision / 100, chip.revision % 100, chip.cores,
             flash_size / (1024 * 1024), esp_get_idf_version());
    ESP_LOGI(TAG, "Wi-Fi STA MAC " MACSTR " (ESP-NOW source address)", MAC2STR(mac));
    if (reason == ESP_RST_BROWNOUT || reason == ESP_RST_PANIC ||
        reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT) {
        ESP_LOGW(TAG, "reset reason: %s (check power supply and wiring)", reset_reason_name(reason));
    } else {
        ESP_LOGI(TAG, "reset reason: %s", reset_reason_name(reason));
    }
}

static void status_led_toggle(void *arg)
{
    static bool on;
    on = !on;
    gpio_set_level((gpio_num_t)CONFIG_RB_NODE_STATUS_LED_GPIO, on);
}

static void status_led_start(void)
{
    if (CONFIG_RB_NODE_STATUS_LED_GPIO == RB_GPIO_NONE) {
        return;
    }
    const gpio_config_t led = {
        .pin_bit_mask = 1ULL << CONFIG_RB_NODE_STATUS_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&led));
    const esp_timer_create_args_t args = {.callback = status_led_toggle, .name = "status_led"};
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 500 * 1000));
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    log_board_info();
    status_led_start();

    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONFIG_RB_NODE_HEALTH_LOG_PERIOD_MS));
        ESP_LOGI(TAG, "uptime %" PRIu64 " s, free heap %" PRIu32 " B, min free %" PRIu32 " B",
                 (uint64_t)(esp_timer_get_time() / 1000000), esp_get_free_heap_size(),
                 esp_get_minimum_free_heap_size());
    }
}
