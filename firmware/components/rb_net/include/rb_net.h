#pragma once

/*
 * Controller network connectivity (TODO section 17): W5500 Ethernet by
 * default, Wi-Fi station as the alternative (menuconfig RB_NET_TYPE).
 *
 * The network only carries telemetry. Nothing on the safety path waits for
 * it, and losing it is a TELEMETRY-class fault.
 */
#include <stdbool.h>
#include "esp_err.h"
#include "esp_netif_ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RB_NET_DISABLED = 0,   /* RB_NET_NONE */
    RB_NET_DOWN,           /* no link / not associated */
    RB_NET_LINK_UP,        /* link up, waiting for DHCP */
    RB_NET_CONNECTED,      /* IP address obtained */
} rb_net_state_t;

typedef void (*rb_net_state_cb_t)(rb_net_state_t state, void *ctx);

/* Bring up the configured interface. Reconnects are handled internally from then on. */
esp_err_t rb_net_start(rb_net_state_cb_t cb, void *ctx);

rb_net_state_t rb_net_state(void);
const char *rb_net_state_name(rb_net_state_t state);
const char *rb_net_interface_name(void);

/* IPv4 address (0.0.0.0 if not connected). */
esp_ip4_addr_t rb_net_ip(void);

#ifdef __cplusplus
}
#endif
