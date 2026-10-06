#include "servo.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "SERVO";

#define SERVO_MODE LEDC_LOW_SPEED_MODE
#define SERVO_TIMER LEDC_TIMER_0
#define SERVO_CHANNEL LEDC_CHANNEL_0
#define SERVO_FREQ_HZ 50
#define SERVO_PERIOD_US (1000000u / SERVO_FREQ_HZ)
/* 14 bits is the widest resolution every ESP32 variant supports at 50 Hz (about 1.2 us per step). */
#define SERVO_RESOLUTION LEDC_TIMER_14_BIT
#define SERVO_DUTY_MAX (1u << 14)

static servo_config_t s_cfg;
static bool s_ready;

esp_err_t servo_init(const servo_config_t *config)
{
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_OUTPUT_GPIO(config->gpio), ESP_ERR_INVALID_ARG, TAG,
                        "GPIO %d can't drive a servo on this chip", config->gpio);
    ESP_RETURN_ON_FALSE(config->min_pulse_us < config->max_pulse_us && config->max_pulse_us < SERVO_PERIOD_US &&
                            config->range_deg > 0,
                        ESP_ERR_INVALID_ARG, TAG, "bad pulse range %lu-%lu us over %lu deg",
                        (unsigned long)config->min_pulse_us, (unsigned long)config->max_pulse_us,
                        (unsigned long)config->range_deg);
    s_cfg = *config;

    const ledc_timer_config_t timer = {
        .speed_mode = SERVO_MODE,
        .timer_num = SERVO_TIMER,
        .duty_resolution = SERVO_RESOLUTION,
        .freq_hz = SERVO_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "timer");
    const ledc_channel_config_t channel = {
        .gpio_num = config->gpio,
        .speed_mode = SERVO_MODE,
        .channel = SERVO_CHANNEL,
        .timer_sel = SERVO_TIMER,
        .duty = 0, /* no pulse until the first servo_set_angle() */
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "channel");
    s_ready = true;
    ESP_LOGI(TAG, "servo on GPIO %d: %lu-%lu us over %lu deg", config->gpio, (unsigned long)config->min_pulse_us,
             (unsigned long)config->max_pulse_us, (unsigned long)config->range_deg);
    return ESP_OK;
}

uint32_t servo_pulse_us(uint32_t angle_deg)
{
    if (angle_deg > s_cfg.range_deg) {
        angle_deg = s_cfg.range_deg;
    }
    return s_cfg.min_pulse_us + (s_cfg.max_pulse_us - s_cfg.min_pulse_us) * angle_deg / s_cfg.range_deg;
}

esp_err_t servo_set_angle(uint32_t angle_deg)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_FALSE(angle_deg <= s_cfg.range_deg, ESP_ERR_INVALID_ARG, TAG, "angle %lu > %lu",
                        (unsigned long)angle_deg, (unsigned long)s_cfg.range_deg);
    const uint32_t duty = servo_pulse_us(angle_deg) * SERVO_DUTY_MAX / SERVO_PERIOD_US;
    ESP_RETURN_ON_ERROR(ledc_set_duty(SERVO_MODE, SERVO_CHANNEL, duty), TAG, "duty");
    return ledc_update_duty(SERVO_MODE, SERVO_CHANNEL);
}
