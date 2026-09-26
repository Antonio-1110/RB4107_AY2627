#include "rb_wallclock.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "pcf85063.h"
#include "rb_board_s3.h"
#include "rb_config.h"
#include "rb_time.h"

static const char *TAG = "RTC";

/* Anything before this means "never set". */
#define MIN_VALID_UTC 1735689600 /* 2025-01-01 */

static bool s_rtc_ok;
static volatile bool s_valid;

static void set_system_time(time_t utc)
{
    const struct timeval tv = {.tv_sec = utc, .tv_usec = 0};
    settimeofday(&tv, NULL);
    s_valid = utc >= MIN_VALID_UTC;
}

esp_err_t rb_wallclock_init(void)
{
    setenv("TZ", CONFIG_RB_TIME_TZ, 1);
    tzset();

    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(rb_board_i2c_bus(&bus), TAG, "I2C bus");
    ESP_RETURN_ON_ERROR(pcf85063_init(bus, CONFIG_RB_S3_PCF85063_ADDRESS), TAG, "RTC device");

    time_t utc = 0;
    esp_err_t err = pcf85063_read(&utc);
    s_rtc_ok = err == ESP_OK || err == ESP_ERR_INVALID_STATE;
    if (err == ESP_OK && utc >= MIN_VALID_UTC) {
        set_system_time(utc);
        char iso[32];
        rb_wallclock_iso8601(rb_time_mono_ms(), iso, sizeof(iso));
        ESP_LOGI(TAG, "wall clock restored from PCF85063: %s", iso);
        return ESP_OK;
    }
    if (!s_rtc_ok) {
        ESP_LOGE(TAG, "PCF85063 not responding (%s)", esp_err_to_name(err));
        return err;
    }
    ESP_LOGW(TAG, "RTC time invalid (oscillator stopped / never set); waiting for SNTP");
#if CONFIG_RB_TIME_SET_RTC_FROM_BUILD
    ESP_LOGW(TAG, "setting RTC to firmware build time (bench testing option)");
    return rb_wallclock_set((time_t)RB_BUILD_EPOCH);
#else
    return ESP_OK;
#endif
}

esp_err_t rb_wallclock_set(time_t utc)
{
    set_system_time(utc);
    return s_rtc_ok ? pcf85063_write(utc) : ESP_ERR_INVALID_STATE;
}

static void on_sntp_sync(struct timeval *tv)
{
    rb_wallclock_set(tv->tv_sec);
    ESP_LOGI(TAG, "SNTP synchronised; RTC updated");
}

esp_err_t rb_wallclock_start_sntp(void)
{
    if (CONFIG_RB_TIME_NTP_SERVER[0] == '\0') {
        return ESP_OK;
    }
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_RB_TIME_NTP_SERVER);
    cfg.sync_cb = on_sntp_sync;
    return esp_netif_sntp_init(&cfg);
}

bool rb_wallclock_valid(void)
{
    return s_valid;
}

bool rb_wallclock_iso8601(uint32_t mono_ms, char *buf, size_t len)
{
    if (len > 0) {
        buf[0] = '\0';
    }
    if (!s_valid || len < 26) {
        return false;
    }
    /* Wall time of the event = now - (mono_now - mono_event). */
    struct timeval now;
    gettimeofday(&now, NULL);
    const int64_t age_ms = (int64_t)(uint32_t)(rb_time_mono_ms() - mono_ms);
    const int64_t event_ms = (int64_t)now.tv_sec * 1000 + now.tv_usec / 1000 - age_ms;
    const time_t secs = (time_t)(event_ms / 1000);

    struct tm local;
    localtime_r(&secs, &local);
    char offset[8];
    strftime(offset, sizeof(offset), "%z", &local); /* +0800 */
    const size_t n = strftime(buf, len, "%Y-%m-%dT%H:%M:%S", &local);
    snprintf(buf + n, len - n, "%c%c%c:%c%c", offset[0], offset[1], offset[2], offset[3], offset[4]);
    return true;
}
