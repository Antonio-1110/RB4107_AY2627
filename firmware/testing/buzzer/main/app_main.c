/*
 * TODO section 13: buzzer driver.
 *
 * Plays each pattern for a few seconds: init, ON, OFF, and the non-blocking
 * warning/shutdown/fault patterns the safety states use. The main loop only
 * picks the next pattern; the esp_timer does the beeping.
 */
#include "buzzer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "OUTPUT";

#define STEP_MS 4000

void app_main(void)
{
    const buzzer_config_t cfg = buzzer_config_from_kconfig();
    ESP_ERROR_CHECK(buzzer_init(&cfg));

    static const buzzer_pattern_t demo[] = {
        BUZZER_PATTERN_CHIRP, BUZZER_PATTERN_ON, BUZZER_PATTERN_OFF, BUZZER_PATTERN_WARNING,
        BUZZER_PATTERN_SHUTDOWN, BUZZER_PATTERN_FAULT, BUZZER_PATTERN_OFF,
    };
    for (;;) {
        for (size_t i = 0; i < sizeof(demo) / sizeof(demo[0]); i++) {
            ESP_LOGI(TAG, "buzzer pattern: %s", buzzer_pattern_name(demo[i]));
            buzzer_set_pattern(demo[i]);
            vTaskDelay(pdMS_TO_TICKS(STEP_MS)); /* demo pacing only; the pattern plays on its own */
        }
    }
}
