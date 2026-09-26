#pragma once

/*
 * Board checks every C6 node runs (TODO section 1): a boot report (chip,
 * Wi-Fi MAC, reset reason), a periodic uptime/heap line and a blinking
 * status LED. A brownout or watchdog reset is logged as a warning because it
 * points to power or wiring problems.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* what: e.g. "presence node" (printed in the boot report). */
void rb_node_board_start(const char *what);

#ifdef __cplusplus
}
#endif
