#pragma once

/*
 * Central configuration entry point (TODO section 26).
 *
 * Every tunable is a Kconfig option in components/rb_config/Kconfig
 * (idf.py menuconfig -> "RB4107 configuration"). Code includes this header
 * rather than scattering literal numbers around.
 */
#include "sdkconfig.h"

#define RB_GPIO_NONE (-1)
