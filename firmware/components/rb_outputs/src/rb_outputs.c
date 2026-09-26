#include "rb_outputs.h"

#include "buzzer.h"
#include "esp_check.h"
#include "esp_log.h"
#include "rb_config.h"
#include "rb_time.h"
#include "shutdown_output.h"

static const char *TAG = "OUTPUT";

static bool s_relay_ok;
static bool s_relay_fault;
static uint32_t s_last_verify_ms;

static buzzer_pattern_t pattern_for(safety_buzzer_t request)
{
    switch (request) {
    case SAFETY_BUZZER_WARNING: return BUZZER_PATTERN_WARNING;
    case SAFETY_BUZZER_SHUTDOWN: return BUZZER_PATTERN_SHUTDOWN;
    case SAFETY_BUZZER_FAULT: return BUZZER_PATTERN_FAULT;
    default: return BUZZER_PATTERN_OFF;
    }
}

esp_err_t rb_outputs_init(void)
{
#if CONFIG_RB_OUTPUTS_SIMULATED
    ESP_LOGW(TAG, "outputs SIMULATED: buzzer and relay are only logged (diagnostics only)");
    return ESP_OK;
#endif
    const buzzer_config_t bcfg = buzzer_config_from_kconfig();
    esp_err_t berr = buzzer_init(&bcfg);
    if (berr != ESP_OK) {
        ESP_LOGE(TAG, "buzzer init failed: %s", esp_err_to_name(berr));
    }
    const shutdown_output_config_t scfg = shutdown_output_config_from_kconfig();
    esp_err_t serr = shutdown_output_init(&scfg);
    s_relay_ok = serr == ESP_OK;
    s_relay_fault = !s_relay_ok;
    if (!s_relay_ok) {
        ESP_LOGE(TAG, "shutdown relay init failed: %s", esp_err_to_name(serr));
    }
    return serr != ESP_OK ? serr : berr;
}

void rb_outputs_apply(const safety_outputs_t *outputs)
{
#if CONFIG_RB_OUTPUTS_SIMULATED
    static safety_outputs_t last = {.buzzer = SAFETY_BUZZER_OFF, .shutdown = false};
    if (outputs->buzzer != last.buzzer || outputs->shutdown != last.shutdown) {
        ESP_LOGW(TAG, "[simulated] buzzer=%s shutdown=%s", safety_buzzer_name(outputs->buzzer),
                 outputs->shutdown ? "ACTIVE" : "released");
        last = *outputs;
    }
    return;
#endif
    buzzer_set_pattern(pattern_for(outputs->buzzer));
    if (!s_relay_ok) {
        return;
    }
    esp_err_t err = outputs->shutdown ? shutdown_activate() : shutdown_release();
    const uint32_t now = rb_time_mono_ms();
    if (err == ESP_OK && rb_time_elapsed(now, s_last_verify_ms, CONFIG_RB_SHUTDOWN_VERIFY_PERIOD_MS)) {
        s_last_verify_ms = now;
        err = shutdown_verify();
    }
    if ((err != ESP_OK) != s_relay_fault) {
        s_relay_fault = err != ESP_OK;
        if (s_relay_fault) {
            ESP_LOGE(TAG, "shutdown relay fault: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "shutdown relay fault cleared");
        }
    }
}

bool rb_outputs_fault(void)
{
    return s_relay_fault;
}
