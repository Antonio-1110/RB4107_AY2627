#include "c4002.h"

#include <string.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "C4002";

#define C4002_RX_BUF 512
#define C4002_TASK_STACK 3072
#define C4002_TASK_PRIO 6

static c4002_config_t s_cfg;
static QueueHandle_t s_resp_queue;
static SemaphoreHandle_t s_cmd_lock;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static c4002_result_t s_result;
static bool s_has_result;
static uint32_t s_result_ms;
static c4002_stats_t s_stats;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void handle_frame(const c4002_frame_t *frame)
{
    if (frame->frame_type == C4002_FRAME_NOTIFICATION) {
        c4002_result_t result;
        if (c4002_decode_result(frame, &result)) {
            portENTER_CRITICAL(&s_lock);
            s_result = result;
            s_has_result = true;
            s_result_ms = now_ms();
            s_stats.results++;
            portEXIT_CRITICAL(&s_lock);
        } else if (frame->cmd == C4002_NOTE_CALIBRATION && frame->data_len >= 2) {
            ESP_LOGI(TAG, "environment calibration: %u s remaining", frame->data[0] | (frame->data[1] << 8));
        }
    } else if (frame->frame_type == C4002_FRAME_WRITE_RESPONSE || frame->frame_type == C4002_FRAME_READ_RESPONSE) {
        xQueueOverwrite(s_resp_queue, frame);
    }
}

static void reader_task(void *arg)
{
    c4002_parser_t parser;
    c4002_parser_reset(&parser);
    c4002_frame_t frame;
    uint8_t buf[64];
    for (;;) {
        int n = uart_read_bytes(s_cfg.uart_port, buf, sizeof(buf), pdMS_TO_TICKS(50));
        for (int i = 0; i < n; i++) {
            switch (c4002_parser_feed(&parser, buf[i], &frame)) {
            case C4002_PARSE_FRAME:
                s_stats.frames_ok++;
                handle_frame(&frame);
                break;
            case C4002_PARSE_ERR_CHECKSUM:
                s_stats.checksum_errors++;
                break;
            case C4002_PARSE_ERR_LENGTH:
                s_stats.length_errors++;
                break;
            default:
                break;
            }
        }
    }
}

esp_err_t c4002_init(const c4002_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "no config");
    s_cfg = *config;

    const uart_config_t uart_cfg = {
        .baud_rate = (int)config->baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(uart_driver_install(config->uart_port, C4002_RX_BUF, 0, 0, NULL, 0), TAG, "uart install");
    ESP_RETURN_ON_ERROR(uart_param_config(config->uart_port, &uart_cfg), TAG, "uart config");
    ESP_RETURN_ON_ERROR(uart_set_pin(config->uart_port, config->tx_gpio, config->rx_gpio,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), TAG, "uart pins");

    if (config->out_gpio >= 0) {
        const gpio_config_t out_pin = {
            .pin_bit_mask = 1ULL << config->out_gpio,
            .mode = GPIO_MODE_INPUT,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&out_pin), TAG, "OUT pin");
    }

    s_resp_queue = xQueueCreate(1, sizeof(c4002_frame_t));
    s_cmd_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_resp_queue && s_cmd_lock, ESP_ERR_NO_MEM, TAG, "no memory");
    ESP_RETURN_ON_FALSE(xTaskCreate(reader_task, "c4002_rx", C4002_TASK_STACK, NULL, C4002_TASK_PRIO, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "task");
    ESP_LOGI(TAG, "UART%d rx=%d tx=%d @%lu baud", config->uart_port, config->rx_gpio, config->tx_gpio,
             (unsigned long)config->baud);
    return ESP_OK;
}

esp_err_t c4002_command(uint8_t frame_type, uint8_t cmd, const uint8_t *data, uint16_t data_len,
                        c4002_frame_t *resp, uint32_t timeout_ms)
{
    uint8_t tx[C4002_MAX_FRAME];
    size_t len = c4002_build_frame(frame_type, cmd, data, data_len, tx, sizeof(tx));
    ESP_RETURN_ON_FALSE(len > 0, ESP_ERR_INVALID_SIZE, TAG, "command too long");
    ESP_RETURN_ON_FALSE(s_cmd_lock != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");

    xSemaphoreTake(s_cmd_lock, portMAX_DELAY);
    xQueueReset(s_resp_queue);
    uart_write_bytes(s_cfg.uart_port, tx, len);

    esp_err_t err = ESP_ERR_TIMEOUT;
    c4002_frame_t frame;
    const int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (esp_timer_get_time() < deadline) {
        TickType_t wait = pdMS_TO_TICKS((deadline - esp_timer_get_time()) / 1000) + 1;
        if (xQueueReceive(s_resp_queue, &frame, wait) != pdTRUE) {
            continue;
        }
        if (frame.cmd != cmd) {
            continue; /* late answer to an earlier command */
        }
        err = frame.resp_code == C4002_RESP_OK ? ESP_OK : ESP_FAIL;
        if (resp != NULL) {
            *resp = frame;
        }
        break;
    }
    xSemaphoreGive(s_cmd_lock);

    if (err == ESP_ERR_TIMEOUT) {
        s_stats.command_timeouts++;
    } else if (err != ESP_OK) {
        s_stats.command_errors++;
    }
    return err;
}

static esp_err_t write_cmd(uint8_t cmd, const uint8_t *data, uint16_t len)
{
    esp_err_t err = c4002_command(C4002_FRAME_WRITE_REQUEST, cmd, data, len, NULL, 300);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "command 0x%02x failed: %s", cmd, esp_err_to_name(err));
    }
    return err;
}

esp_err_t c4002_apply_settings(const c4002_settings_t *s)
{
    const uint16_t max_cm = s->range_max_cm > 1100 ? 1100 : s->range_max_cm;
    ESP_RETURN_ON_FALSE(s->range_min_cm <= max_cm, ESP_ERR_INVALID_ARG, TAG, "bad range");

    const uint8_t leds[] = {s->run_led, s->out_led};
    const uint8_t resolution[] = {(uint8_t)s->resolution};
    const uint8_t range[] = {s->range_min_cm & 0xFF, s->range_min_cm >> 8, max_cm & 0xFF, max_cm >> 8};
    const uint8_t motion_sens[] = {C4002_GATE_MOTION, (uint8_t)s->motion_sensitivity};
    const uint8_t presence_sens[] = {C4002_GATE_PRESENCE, (uint8_t)s->presence_sensitivity};
    const uint8_t delay[] = {s->disappear_delay_s & 0xFF, s->disappear_delay_s >> 8};
    const uint8_t period[] = {s->report_period_ds};

    ESP_RETURN_ON_ERROR(write_cmd(C4002_CMD_LED_MODE, leds, sizeof(leds)), TAG, "LEDs");
    ESP_RETURN_ON_ERROR(write_cmd(C4002_CMD_RESOLUTION_MODE, resolution, sizeof(resolution)), TAG, "resolution");
    ESP_RETURN_ON_ERROR(write_cmd(C4002_CMD_DETECT_RANGE, range, sizeof(range)), TAG, "range");
    ESP_RETURN_ON_ERROR(write_cmd(C4002_CMD_THRESHOLD_GROUP, motion_sens, sizeof(motion_sens)), TAG, "motion sensitivity");
    ESP_RETURN_ON_ERROR(write_cmd(C4002_CMD_THRESHOLD_GROUP, presence_sens, sizeof(presence_sens)), TAG, "presence sensitivity");
    ESP_RETURN_ON_ERROR(write_cmd(C4002_CMD_TARGET_DISAPPEAR_DELAY, delay, sizeof(delay)), TAG, "disappear delay");
    ESP_RETURN_ON_ERROR(write_cmd(C4002_CMD_REPORT_PERIOD, period, sizeof(period)), TAG, "report period");
    ESP_LOGI(TAG, "settings applied: range %u-%u cm, sensitivity motion=%d presence=%d, hold %u s, report %u00 ms",
             s->range_min_cm, max_cm, s->motion_sensitivity, s->presence_sensitivity, s->disappear_delay_s,
             s->report_period_ds);
    return ESP_OK;
}

esp_err_t c4002_start_env_calibration(uint16_t delay_s, uint16_t duration_s)
{
    const uint8_t data[] = {delay_s & 0xFF, delay_s >> 8, duration_s & 0xFF, duration_s >> 8, 0x01};
    return write_cmd(C4002_CMD_ENV_CALIBRATION, data, sizeof(data));
}

bool c4002_get_reading(presence_reading_t *out)
{
    c4002_result_t result;
    uint32_t age_ms;
    if (!c4002_get_raw(&result, &age_ms) || age_ms > s_cfg.stale_timeout_ms) {
        memset(out, 0, sizeof(*out));
        out->timestamp_ms = now_ms();
        return false;
    }
    c4002_result_to_presence(&result, now_ms() - age_ms, out);
    if (!out->valid) {
        s_stats.invalid_results++;
    }
    return out->valid;
}

bool c4002_get_raw(c4002_result_t *out, uint32_t *age_ms)
{
    portENTER_CRITICAL(&s_lock);
    bool has = s_has_result;
    *out = s_result;
    uint32_t at = s_result_ms;
    portEXIT_CRITICAL(&s_lock);
    if (age_ms != NULL) {
        *age_ms = now_ms() - at;
    }
    return has;
}

int c4002_get_out_level(void)
{
    return s_cfg.out_gpio >= 0 ? gpio_get_level(s_cfg.out_gpio) : -1;
}

void c4002_get_stats(c4002_stats_t *out)
{
    *out = s_stats;
}
