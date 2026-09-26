#include "rb_safety_config.h"

#include <stddef.h>

#include "rb_config.h"

safety_config_t rb_safety_config_from_kconfig(void)
{
    return (safety_config_t){
        .self_test_timeout_ms = CONFIG_RB_SAFETY_SELF_TEST_TIMEOUT_MS,
        .heat_on_temp_c = CONFIG_RB_SAFETY_HEAT_ON_DC / 10.0f,
        .heat_off_temp_c = CONFIG_RB_SAFETY_HEAT_OFF_DC / 10.0f,
        .heat_on_rate_c_per_min = CONFIG_RB_SAFETY_HEAT_ON_RATE_DC_PER_MIN / 10.0f,
        .absence_debounce_ms = CONFIG_RB_SAFETY_ABSENCE_DEBOUNCE_MS,
        .presence_return_debounce_ms = CONFIG_RB_SAFETY_PRESENCE_RETURN_DEBOUNCE_MS,
        .warning_timeout_ms = CONFIG_RB_SAFETY_WARNING_TIMEOUT_S * 1000u,
        .shutdown_timeout_ms = CONFIG_RB_SAFETY_SHUTDOWN_TIMEOUT_S * 1000u,
#if CONFIG_RB_SAFETY_SHUTDOWN_FROM_WARNING
        .shutdown_timing = SAFETY_SHUTDOWN_AFTER_WARNING_START,
#else
        .shutdown_timing = SAFETY_SHUTDOWN_AFTER_UNATTENDED_START,
#endif
#if CONFIG_RB_SAFETY_WARNING_EXIT_ACK
        .warning_exit = SAFETY_WARNING_EXIT_ON_ACK,
#else
        .warning_exit = SAFETY_WARNING_EXIT_ON_PRESENCE,
#endif
#if CONFIG_RB_SAFETY_COOLING_RETURN_TO_IDLE
        .cooling_policy = SAFETY_COOLING_RETURN_TO_IDLE,
#else
        .cooling_policy = SAFETY_COOLING_KEEP_TIMERS,
#endif
        .fault_shutdown_timeout_ms = CONFIG_RB_SAFETY_FAULT_SHUTDOWN_S * 1000u,
        .timing_hook = NULL,
        .timing_hook_ctx = NULL,
    };
}
