#pragma once

/*
 * When the valve node opens and closes the gas valve. Both modes fail
 * closed: the valve starts closed and only opens on a positive "safe" signal.
 *
 * Wired: the controller output pulls the input low while it is safe. Low for
 * open_delay_ms opens the valve; high for close_debounce_ms closes it. A cut
 * wire or a controller with no power reads high, so the valve closes.
 *
 * Wireless: the controller sends KEEP_OPEN about once a second while it is
 * safe. KEEP_OPEN opens the valve; CLOSE, or no KEEP_OPEN for
 * link_timeout_ms, closes it. A dropped radio or a crashed controller ends
 * in a closed valve.
 *
 * With latch_closed, the first close after the valve has been open is final
 * until the node is reset.
 */
#include <stdbool.h>
#include <stdint.h>
#include "rb_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VALVE_MODE_WIRED,
    VALVE_MODE_WIRELESS,
} valve_mode_t;

typedef struct {
    valve_mode_t mode;
    uint32_t open_delay_ms;      /* wired */
    uint32_t close_debounce_ms;  /* wired */
    uint32_t link_timeout_ms;    /* wireless */
    bool latch_closed;
} valve_logic_config_t;

typedef struct {
    valve_logic_config_t cfg;
    bool open;
    rb_valve_reason_t reason;    /* why it is closed (NONE while open) */
    bool latched;
    uint32_t changed_ms;         /* when it last opened or closed */
    /* wireless */
    bool keep_open;
    uint32_t last_keep_open_ms;
    /* wired */
    bool line_low;
    uint32_t line_since_ms;
} valve_logic_t;

/* Starts closed (reason BOOT). */
void valve_logic_init(valve_logic_t *v, const valve_logic_config_t *cfg, uint32_t now_ms);

/* Wireless: a VALVE_COMMAND addressed to this node arrived. Anything but KEEP_OPEN closes. */
void valve_logic_on_command(valve_logic_t *v, rb_valve_cmd_t command, uint32_t now_ms);

/* Wired: the current input level (true = pulled low = safe). Call it every sample. */
void valve_logic_on_line(valve_logic_t *v, bool line_low, uint32_t now_ms);

/* Run the timers. Returns true if the valve should be open. */
bool valve_logic_tick(valve_logic_t *v, uint32_t now_ms);

const char *valve_reason_name(rb_valve_reason_t reason);

#ifdef __cplusplus
}
#endif
