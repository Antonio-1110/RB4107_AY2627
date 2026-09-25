#include "rb_log.h"

#include <string.h>
#include "rb_config.h"

static const char *TAG = "LOG";

static void apply(const char *list, esp_log_level_t level)
{
    char tags[128];
    strlcpy(tags, list, sizeof(tags));
    char *save = NULL;
    for (char *tag = strtok_r(tags, ", ", &save); tag != NULL; tag = strtok_r(NULL, ", ", &save)) {
        esp_log_level_set(tag, level);
        ESP_LOGI(TAG, "tag %s -> %s", tag, level == ESP_LOG_DEBUG ? "DEBUG" : "WARN");
    }
}

void rb_log_init(void)
{
    apply(CONFIG_RB_LOG_DEBUG_TAGS, ESP_LOG_DEBUG);
    apply(CONFIG_RB_LOG_QUIET_TAGS, ESP_LOG_WARN);
}
