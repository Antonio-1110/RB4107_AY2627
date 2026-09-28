/*
 * TODO section 15: RTC and time.
 *
 * Restores the wall clock from the PCF85063 at boot, then prints both kinds
 * of time every 5 s:
 *   - monotonic ms since boot (all safety timing),
 *   - wall-clock ISO 8601 (timestamps only), plus the raw RTC reading.
 *
 * Persistence test: set the RTC (SNTP in a networked build, or the
 * RB_TIME_SET_RTC_FROM_BUILD bench option), power the board off for a few
 * minutes, power it on, and check the restored time is still right.
 */
#include <inttypes.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pcf85063.h"
#include "rb_time.h"
#include "rb_wallclock.h"

static const char *TAG = "RTC";

void app_main(void)
{
    if (rb_wallclock_init() != ESP_OK) {
        ESP_LOGE(TAG, "RTC unavailable: wall-clock timestamps will be missing (safety timing is unaffected)");
    }
    for (;;) {
        char iso[32];
        const uint32_t mono = rb_time_mono_ms();
        const bool valid = rb_wallclock_iso8601(mono, iso, sizeof(iso));
        time_t rtc_utc = 0;
        const esp_err_t rtc = pcf85063_read(&rtc_utc);
        ESP_LOGI(TAG, "monotonic %" PRIu32 " ms | wall %s | RTC %s (%lld)", mono, valid ? iso : "(not set)",
                 rtc == ESP_OK ? "ok" : esp_err_to_name(rtc), (long long)rtc_utc);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
