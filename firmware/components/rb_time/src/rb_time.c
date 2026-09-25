#include "rb_time.h"

#include "esp_timer.h"

uint32_t rb_time_mono_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

int64_t rb_time_mono_us(void)
{
    return esp_timer_get_time();
}
