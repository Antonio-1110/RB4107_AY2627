#pragma once

#include "safety.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Safety configuration as set in menuconfig ("Safety logic"). */
safety_config_t rb_safety_config_from_kconfig(void);

#ifdef __cplusplus
}
#endif
