#include "rb_time.h"

#include <stdio.h>
#include "esp_random.h"
#include "esp_timer.h"

uint32_t rb_time_mono_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

int64_t rb_time_mono_us(void)
{
    return esp_timer_get_time();
}

const char *rb_time_boot_id(void)
{
    static char id[9];
    if (id[0] == '\0') {
        snprintf(id, sizeof(id), "%08lx", (unsigned long)esp_random());
    }
    return id;
}
