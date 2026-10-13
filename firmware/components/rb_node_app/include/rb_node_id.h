#pragma once

/*
 * This node's ID. Both presence nodes run the same build: at boot each one
 * looks up its own Wi-Fi STA MAC in RB_NODE_ID_BY_MAC and takes the ID listed
 * for it, or RB_NODE_ID when its MAC isn't listed.
 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The node ID every packet carries. Resolved once, on the first call. */
uint32_t rb_node_id(void);

/*
 * The ID listed for mac in map ("AA:BB:CC:DD:EE:FF=1, 11:22:33:44:55:66=2",
 * case-insensitive, entries separated by commas or spaces), or fallback when
 * mac isn't listed or map is malformed. Pure, for the unit tests.
 */
uint32_t rb_node_id_lookup(const char *map, const uint8_t mac[6], uint32_t fallback);

#ifdef __cplusplus
}
#endif
