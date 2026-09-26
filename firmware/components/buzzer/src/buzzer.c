#include "buzzer.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

static const char *TAG = "OUTPUT";

#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_RESOLUTION LEDC_TIMER_10_BIT
#define LEDC_HALF_DUTY 512

/* Each pattern: alternating on/off durations in ms, 0-terminated. */
typedef struct {
    uint16_t steps[6];
    bool repeat;
} pattern_def_t;

static const pattern_def_t PATTERNS[BUZZER_PATTERN_COUNT] = {
    [BUZZER_PATTERN_OFF] = {{0}, false},
    [BUZZER_PATTERN_ON] = {{0}, false},
    [BUZZER_PATTERN_WARNING] = {{200, 800, 0}, true},
    [BUZZER_PATTERN_SHUTDOWN] = {{150, 150, 0}, true},
    [BUZZER_PATTERN_FAULT] = {{100, 100, 100, 1700, 0}, true},
    [BUZZER_PATTERN_CHIRP] = {{100, 0}, false},
};

static buzzer_config_t s_cfg = {.gpio = -1};
static esp_timer_handle_t s_timer;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static buzzer_pattern_t s_pattern = BUZZER_PATTERN_OFF;
static uint8_t s_step;

buzzer_config_t buzzer_config_from_kconfig(void)
{
    return (buzzer_config_t){
        .gpio = CONFIG_RB_BUZZER_GPIO,
        .active_level = CONFIG_RB_BUZZER_ACTIVE_LEVEL,
#if CONFIG_RB_BUZZER_DRIVE_PWM
        .pwm = true,
        .tone_hz = CONFIG_RB_BUZZER_TONE_HZ,
#else
        .pwm = false,
        .tone_hz = 0,
#endif
    };
}

static void drive(bool on)
{
    if (s_cfg.gpio < 0) {
        return;
    }
    if (s_cfg.pwm) {
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, on ? LEDC_HALF_DUTY : 0);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    } else {
        gpio_set_level(s_cfg.gpio, on ? s_cfg.active_level : !s_cfg.active_level);
    }
}

/* esp_timer callback: play the current step, then schedule the next. */
static void pattern_tick(void *arg)
{
    portENTER_CRITICAL(&s_lock);
    const pattern_def_t *p = &PATTERNS[s_pattern];
    if (p->steps[s_step] == 0) {
        if (!p->repeat) {
            portEXIT_CRITICAL(&s_lock);
            drive(false);
            return;
        }
        s_step = 0;
    }
    const bool on = (s_step % 2) == 0;
    const uint32_t ms = p->steps[s_step++];
    portEXIT_CRITICAL(&s_lock);

    drive(on);
    esp_timer_start_once(s_timer, (uint64_t)ms * 1000);
}

esp_err_t buzzer_init(const buzzer_config_t *config)
{
    s_cfg = *config;
    if (s_cfg.gpio < 0) {
        ESP_LOGW(TAG, "no buzzer configured");
        return ESP_OK;
    }
    if (s_cfg.pwm) {
        const ledc_timer_config_t timer = {
            .speed_mode = LEDC_MODE,
            .duty_resolution = LEDC_RESOLUTION,
            .timer_num = LEDC_TIMER,
            .freq_hz = s_cfg.tone_hz,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "ledc timer");
        const ledc_channel_config_t channel = {
            .gpio_num = s_cfg.gpio,
            .speed_mode = LEDC_MODE,
            .channel = LEDC_CHANNEL,
            .timer_sel = LEDC_TIMER,
            .duty = 0,
        };
        ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "ledc channel");
    } else {
        /* Set the idle level before switching the pin to output, so it never glitches. */
        gpio_set_level(s_cfg.gpio, !s_cfg.active_level);
        const gpio_config_t io = {.pin_bit_mask = 1ULL << s_cfg.gpio, .mode = GPIO_MODE_OUTPUT};
        ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio");
    }
    const esp_timer_create_args_t args = {.callback = pattern_tick, .name = "buzzer"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&args, &s_timer), TAG, "timer");
    drive(false);
    ESP_LOGI(TAG, "buzzer on GPIO%d (%s, active %s)", s_cfg.gpio, s_cfg.pwm ? "PWM tone" : "DC level",
             s_cfg.active_level ? "high" : "low");
    return ESP_OK;
}

void buzzer_set_pattern(buzzer_pattern_t pattern)
{
    if (pattern >= BUZZER_PATTERN_COUNT || s_timer == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    const bool changed = pattern != s_pattern;
    s_pattern = pattern;
    s_step = 0;
    portEXIT_CRITICAL(&s_lock);
    if (!changed) {
        return;
    }
    esp_timer_stop(s_timer); /* ESP_ERR_INVALID_STATE if it wasn't running: fine */
    if (pattern == BUZZER_PATTERN_OFF) {
        drive(false);
    } else if (pattern == BUZZER_PATTERN_ON) {
        drive(true);
    } else {
        pattern_tick(NULL);
    }
}

void buzzer_on(void)
{
    buzzer_set_pattern(BUZZER_PATTERN_ON);
}

void buzzer_off(void)
{
    buzzer_set_pattern(BUZZER_PATTERN_OFF);
}

buzzer_pattern_t buzzer_get_pattern(void)
{
    return s_pattern;
}

const char *buzzer_pattern_name(buzzer_pattern_t pattern)
{
    static const char *const names[BUZZER_PATTERN_COUNT] = {"OFF", "ON", "WARNING", "SHUTDOWN", "FAULT", "CHIRP"};
    return pattern < BUZZER_PATTERN_COUNT ? names[pattern] : "?";
}
