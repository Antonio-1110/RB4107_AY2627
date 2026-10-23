#include "rb_log_capture.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "rb_config.h"

#if CONFIG_RB_MQTT_PUBLISH_LOG

#define LINE_BUF 192

static rb_log_line_t s_ring[CONFIG_RB_MQTT_LOG_BUFFER_LINES];
static size_t s_head;  /* oldest line */
static size_t s_count;
static char s_line[LINE_BUF]; /* a line being put together (ESP-IDF may print one in pieces) */
static size_t s_line_len;
static SemaphoreHandle_t s_mutex;
static vprintf_like_t s_serial;
static portMUX_TYPE s_drop_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_dropped;

static void count_drop(void)
{
    portENTER_CRITICAL_SAFE(&s_drop_lock);
    s_dropped++;
    portEXIT_CRITICAL_SAFE(&s_drop_lock);
}

/* Tags never copied (RB_MQTT_LOG_SKIP_TAGS): the MQTT client's own lines would loop back through the publish. */
static bool skipped(const char *tag)
{
    const char *list = CONFIG_RB_MQTT_LOG_SKIP_TAGS;
    const size_t len = strlen(tag);
    while (*list != '\0') {
        while (*list == ',' || *list == ' ') {
            list++;
        }
        size_t n = strcspn(list, ", ");
        if (n == len && n > 0 && strncmp(list, tag, n) == 0) {
            return true;
        }
        list += n;
    }
    return false;
}

/* Under s_mutex. */
static void store(const char *text, size_t len)
{
    static rb_log_line_t line; /* static: kept off the stack of whichever task logged */
    if (!rb_log_line_parse(text, len, esp_log_timestamp(), &line) || skipped(line.tag)) {
        return;
    }
    if (s_count == CONFIG_RB_MQTT_LOG_BUFFER_LINES) {
        s_head = (s_head + 1) % CONFIG_RB_MQTT_LOG_BUFFER_LINES; /* overwrite the oldest */
        s_count--;
        count_drop();
    }
    s_ring[(s_head + s_count) % CONFIG_RB_MQTT_LOG_BUFFER_LINES] = line;
    s_count++;
}

/* Under s_mutex: append a piece, store every finished line. */
static void collect(const char *fmt, va_list args)
{
    int n = vsnprintf(s_line + s_line_len, sizeof(s_line) - s_line_len, fmt, args);
    if (n < 0) {
        return;
    }
    s_line_len += (size_t)n < sizeof(s_line) - s_line_len ? (size_t)n : sizeof(s_line) - s_line_len - 1;
    char *start = s_line;
    char *nl;
    while ((nl = memchr(start, '\n', s_line_len - (size_t)(start - s_line))) != NULL) {
        store(start, (size_t)(nl - start));
        start = nl + 1;
    }
    const size_t rest = s_line_len - (size_t)(start - s_line);
    if (rest == sizeof(s_line) - 1) {
        store(s_line, rest); /* too long for one line: keep the beginning */
        s_line_len = 0;
    } else {
        memmove(s_line, start, rest);
        s_line_len = rest;
    }
}

static int log_hook(const char *fmt, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    const int ret = s_serial(fmt, args);
    /* Never wait: not from an interrupt or a critical section, and not while another task is copying. */
    if (!xPortInIsrContext() && xPortCanYield() && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING &&
        xSemaphoreTake(s_mutex, 0) == pdTRUE) {
        collect(fmt, copy);
        xSemaphoreGive(s_mutex);
    } else {
        count_drop();
    }
    va_end(copy);
    return ret;
}

esp_err_t rb_log_capture_start(void)
{
    if (s_mutex != NULL) {
        return ESP_OK;
    }
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_serial = esp_log_set_vprintf(log_hook);
    if (s_serial == NULL) {
        s_serial = vprintf;
    }
    return ESP_OK;
}

size_t rb_log_capture_take(rb_log_line_t *out, size_t max)
{
    if (s_mutex == NULL || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return 0;
    }
    size_t n = 0;
    while (n < max && s_count > 0) {
        out[n++] = s_ring[s_head];
        s_head = (s_head + 1) % CONFIG_RB_MQTT_LOG_BUFFER_LINES;
        s_count--;
    }
    xSemaphoreGive(s_mutex);
    return n;
}

uint32_t rb_log_capture_dropped(void)
{
    portENTER_CRITICAL_SAFE(&s_drop_lock);
    const uint32_t n = s_dropped;
    portEXIT_CRITICAL_SAFE(&s_drop_lock);
    return n;
}

#else /* RB_MQTT_PUBLISH_LOG off: nothing is copied */

esp_err_t rb_log_capture_start(void)
{
    return ESP_OK;
}

size_t rb_log_capture_take(rb_log_line_t *out, size_t max)
{
    return 0;
}

uint32_t rb_log_capture_dropped(void)
{
    return 0;
}

#endif
