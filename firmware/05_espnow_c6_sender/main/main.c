/*
 * TODO sections 1 and 5: the complete ESP32-C6 sensor node.
 *
 *   C4002 ──┐
 *           ├─→ C6 → ESP-NOW → S3
 *   MLX90640┘
 *
 * The node only acquires, filters, extracts features and transmits. The
 * safety state machine runs on the S3 (firmware 29). The boot report and the
 * periodic heap/uptime line cover section 1 ("board stays stable after
 * hardware assembly"); the status LED blinks while the firmware runs.
 */
#include <inttypes.h>
#include "driver/gpio.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "node_link.h"
#include "node_thermal.h"
#include "rb_config.h"
#include "rb_log.h"
#include "rb_node_sensors.h"

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
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    const esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGI(TAG, "RB4107 sensor node %d (FireBeetle 2 ESP32-C6 rev v%d.%d, IDF %s)", CONFIG_RB_NODE_ID,
             chip.revision / 100, chip.revision % 100, esp_get_idf_version());
    ESP_LOGI(TAG, "Wi-Fi STA MAC " MACSTR " (ESP-NOW source address)", MAC2STR(mac));
    if (reason == ESP_RST_BROWNOUT || reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
        reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT) {
        ESP_LOGW(TAG, "reset reason: %s (check power supply and wiring)", reset_reason_name(reason));
    } else {
        ESP_LOGI(TAG, "reset reason: %s", reset_reason_name(reason));
    }
}

static void health_log(void *arg)
{
    ESP_LOGI(TAG, "uptime %" PRIu64 " s, free heap %" PRIu32 " B, min free %" PRIu32 " B",
             (uint64_t)(esp_timer_get_time() / 1000000), esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
}

static void status_led_toggle(void *arg)
{
    static bool on;
    on = !on;
    gpio_set_level((gpio_num_t)CONFIG_RB_NODE_STATUS_LED_GPIO, on);
}

/* Periodic jobs on esp_timer: nothing here ever blocks a task. */
static void start_board_timers(void)
{
    esp_timer_handle_t timer;
    const esp_timer_create_args_t health = {.callback = health_log, .name = "health_log"};
    ESP_ERROR_CHECK(esp_timer_create(&health, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, (uint64_t)CONFIG_RB_NODE_HEALTH_LOG_PERIOD_MS * 1000));

    if (CONFIG_RB_NODE_STATUS_LED_GPIO != RB_GPIO_NONE) {
        const gpio_config_t led = {.pin_bit_mask = 1ULL << CONFIG_RB_NODE_STATUS_LED_GPIO, .mode = GPIO_MODE_OUTPUT};
        ESP_ERROR_CHECK(gpio_config(&led));
        const esp_timer_create_args_t blink = {.callback = status_led_toggle, .name = "status_led"};
        ESP_ERROR_CHECK(esp_timer_create(&blink, &timer));
        ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 500 * 1000));
    }
}

void app_main(void)
{
    rb_log_init();
    log_board_info();
    start_board_timers();

    /* A failed sensor is reported as a fault over ESP-NOW; it doesn't stop the node. */
    if (rb_node_c4002_start() != ESP_OK) {
        ESP_LOGE(TAG, "C4002 not configured; presence will be reported INVALID until it responds");
    }
    ESP_ERROR_CHECK(node_thermal_start());
    ESP_ERROR_CHECK(node_link_start());
}
