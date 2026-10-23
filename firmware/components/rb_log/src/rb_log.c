#include "rb_log.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "rb_config.h"

static const char *TAG = "LOG";

static const char *const LEVEL_NAMES[] = {"none", "error", "warn", "info", "debug", "verbose"};

/* Written from the MQTT and console tasks, read by the telemetry task: short copies under the lock. */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static esp_log_level_t s_default = (esp_log_level_t)CONFIG_LOG_DEFAULT_LEVEL;
static rb_log_tag_level_t s_tags[RB_LOG_TRACKED_TAGS];
static size_t s_tag_count;
static uint32_t s_version;

const char *rb_log_level_name(esp_log_level_t level)
{
    return (unsigned)level < sizeof(LEVEL_NAMES) / sizeof(LEVEL_NAMES[0]) ? LEVEL_NAMES[level] : "?";
}

bool rb_log_level_parse(const char *name, esp_log_level_t *out)
{
    for (size_t i = 0; i < sizeof(LEVEL_NAMES) / sizeof(LEVEL_NAMES[0]); i++) {
        if (strcmp(name, LEVEL_NAMES[i]) == 0) {
            *out = (esp_log_level_t)i;
            return true;
        }
    }
    return false;
}

bool rb_log_set_level(const char *tag, esp_log_level_t level)
{
    bool tracked = true;
    if (strcmp(tag, "*") == 0) {
        /* Some ESP-IDF versions drop every per-tag level when the default changes: put them back. */
        rb_log_tag_level_t tags[RB_LOG_TRACKED_TAGS];
        portENTER_CRITICAL(&s_lock);
        s_default = level;
        const size_t n = s_tag_count;
        memcpy(tags, s_tags, sizeof(tags));
        s_version++;
        portEXIT_CRITICAL(&s_lock);
        esp_log_level_set("*", level);
        for (size_t i = 0; i < n; i++) {
            esp_log_level_set(tags[i].tag, tags[i].level);
        }
        return true;
    }
    esp_log_level_set(tag, level);
    portENTER_CRITICAL(&s_lock);
    size_t i = 0;
    while (i < s_tag_count && strcmp(s_tags[i].tag, tag) != 0) {
        i++;
    }
    if (strlen(tag) >= RB_LOG_TAG_LEN || (i == s_tag_count && s_tag_count == RB_LOG_TRACKED_TAGS)) {
        tracked = false;
    } else {
        if (i == s_tag_count) {
            strlcpy(s_tags[i].tag, tag, sizeof(s_tags[i].tag));
            s_tag_count++;
        }
        s_tags[i].level = level;
    }
    s_version++;
    portEXIT_CRITICAL(&s_lock);
    return tracked;
}

static void apply(const char *list, esp_log_level_t level)
{
    char tags[128];
    strlcpy(tags, list, sizeof(tags));
    char *save = NULL;
    for (char *tag = strtok_r(tags, ", ", &save); tag != NULL; tag = strtok_r(NULL, ", ", &save)) {
        rb_log_set_level(tag, level);
        ESP_LOGI(TAG, "tag %s -> %s", tag, level == ESP_LOG_DEBUG ? "DEBUG" : "WARN");
    }
}

void rb_log_init(void)
{
    apply(CONFIG_RB_LOG_DEBUG_TAGS, ESP_LOG_DEBUG);
    apply(CONFIG_RB_LOG_QUIET_TAGS, ESP_LOG_WARN);
}

void rb_log_reset_levels(void)
{
    const esp_log_level_t level = (esp_log_level_t)CONFIG_LOG_DEFAULT_LEVEL;
    rb_log_tag_level_t tags[RB_LOG_TRACKED_TAGS];
    portENTER_CRITICAL(&s_lock);
    const size_t n = s_tag_count;
    memcpy(tags, s_tags, sizeof(tags));
    s_tag_count = 0;
    s_default = level;
    s_version++;
    portEXIT_CRITICAL(&s_lock);
    esp_log_level_set("*", level);
    for (size_t i = 0; i < n; i++) {
        esp_log_level_set(tags[i].tag, level);
    }
    rb_log_init();
}

size_t rb_log_levels(esp_log_level_t *default_level, rb_log_tag_level_t *out, size_t max)
{
    portENTER_CRITICAL(&s_lock);
    *default_level = s_default;
    const size_t n = s_tag_count < max ? s_tag_count : max;
    memcpy(out, s_tags, n * sizeof(*out));
    portEXIT_CRITICAL(&s_lock);
    return n;
}

uint32_t rb_log_levels_version(void)
{
    return s_version;
}
