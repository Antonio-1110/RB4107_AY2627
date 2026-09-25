#pragma once

typedef enum
{
    SYSTEM_NORMAL = 0,
    SYSTEM_WARNING,
    SYSTEM_SHUTDOWN_PENDING,
    SYSTEM_SHUTDOWN,
    SYSTEM_FAULT
} system_state_t;