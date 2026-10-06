#include "valve_logic.h"

#include <string.h>

static bool elapsed(uint32_t now_ms, uint32_t since_ms, uint32_t timeout_ms)
{
    return (uint32_t)(now_ms - since_ms) >= timeout_ms;
}

static void do_open(valve_logic_t *v, uint32_t now_ms)
{
    if (v->open || v->latched) {
        return;
    }
    v->open = true;
    v->reason = RB_VALVE_REASON_NONE;
    v->changed_ms = now_ms;
}

static void do_close(valve_logic_t *v, rb_valve_reason_t reason, uint32_t now_ms)
{
    if (v->open) {
        v->open = false;
        v->latched = v->cfg.latch_closed;
        v->changed_ms = now_ms;
    }
    v->reason = reason;
}

void valve_logic_init(valve_logic_t *v, const valve_logic_config_t *cfg, uint32_t now_ms)
{
    memset(v, 0, sizeof(*v));
    v->cfg = *cfg;
    v->reason = RB_VALVE_REASON_BOOT;
    v->changed_ms = now_ms;
    v->line_since_ms = now_ms;
}

void valve_logic_on_command(valve_logic_t *v, rb_valve_cmd_t command, uint32_t now_ms)
{
    if (v->cfg.mode != VALVE_MODE_WIRELESS) {
        return;
    }
    if (command == RB_VALVE_CMD_KEEP_OPEN) {
        v->keep_open = true;
        v->last_keep_open_ms = now_ms;
        do_open(v, now_ms);
    } else {
        v->keep_open = false;
        do_close(v, RB_VALVE_REASON_COMMAND, now_ms);
    }
}

void valve_logic_on_line(valve_logic_t *v, bool line_low, uint32_t now_ms)
{
    if (v->cfg.mode != VALVE_MODE_WIRED) {
        return;
    }
    if (line_low != v->line_low) {
        v->line_low = line_low;
        v->line_since_ms = now_ms;
    }
}

bool valve_logic_tick(valve_logic_t *v, uint32_t now_ms)
{
    if (v->cfg.mode == VALVE_MODE_WIRELESS) {
        if (v->keep_open && elapsed(now_ms, v->last_keep_open_ms, v->cfg.link_timeout_ms)) {
            v->keep_open = false;
            do_close(v, RB_VALVE_REASON_LINK_TIMEOUT, now_ms);
        }
    } else if (v->line_low) {
        if (elapsed(now_ms, v->line_since_ms, v->cfg.open_delay_ms)) {
            do_open(v, now_ms);
        }
    } else if (elapsed(now_ms, v->line_since_ms, v->cfg.close_debounce_ms)) {
        do_close(v, RB_VALVE_REASON_WIRED_LINE, now_ms);
    }
    return v->open;
}

const char *valve_reason_name(rb_valve_reason_t reason)
{
    switch (reason) {
    case RB_VALVE_REASON_NONE: return "open";
    case RB_VALVE_REASON_BOOT: return "boot";
    case RB_VALVE_REASON_COMMAND: return "controller sent CLOSE";
    case RB_VALVE_REASON_LINK_TIMEOUT: return "no keep-open from the controller";
    case RB_VALVE_REASON_WIRED_LINE: return "input line high";
    default: return "?";
    }
}
