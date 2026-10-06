#pragma once

/*
 * The dashboard -> controller command, published by Django on
 * <prefix>/controller/command. JSON contract in docs/remote_reset.md and
 * docs/schema/rb4107_mqtt.schema.json ("controller_command"). Example:
 *
 *   {"schema_version":2,"type":"controller_command","controller_id":"controller_01",
 *    "request_id":7,"action":"reset"}
 *
 * The only action is "reset": the same operator reset / acknowledge as the
 * diagnostic console's `reset` command. Flat object, strings and integers
 * only, so a small hand-written parser is enough.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RB_CTRL_ACTION_RESET = 1,
} rb_ctrl_action_t;

typedef struct {
    rb_ctrl_action_t action;
    uint16_t request_id;
    char controller_id[40];
} rb_ctrl_cmd_t;

/*
 * Parse and check one command. Every key is required and no other key is
 * allowed. On failure returns false and writes a short reason into err.
 */
bool rb_ctrl_cmd_parse(const char *json, size_t len, rb_ctrl_cmd_t *out, char *err, size_t err_len);

#ifdef __cplusplus
}
#endif
