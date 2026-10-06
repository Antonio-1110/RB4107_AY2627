#pragma once

/*
 * Controller side of the wireless valve node (firmware/valve_node).
 *
 * The safety task reports whether it wants a shutdown on every tick. A task
 * here turns that into VALVE_COMMAND packets: KEEP_OPEN every
 * RB_CTRL_VALVE_KEEPALIVE_MS while safe, CLOSE on shutdown (sent right away,
 * then repeated). If the safety task stops reporting, the link sends CLOSE.
 * If the whole controller dies, the keep-opens stop and the valve node
 * closes on its own timeout. The valve node answers with VALVE_STATUS.
 */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "rb_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Add the valve node as a peer and start the sending task. ESP-NOW must already be started. */
esp_err_t rb_valve_link_start(void);

/* What the safety logic wants right now. Call it every safety tick; never blocks. */
void rb_valve_link_set_shutdown(bool shutdown);

/*
 * A packet from a valve-role sender. Returns false if it is not from the
 * configured valve node (the caller counts it as foreign). Never blocks.
 */
bool rb_valve_link_on_packet(const rb_packet_t *pkt, uint32_t rx_ms);

/* Latest VALVE_STATUS. Returns false if none has arrived yet. */
bool rb_valve_link_get_status(rb_valve_status_t *out, uint32_t *age_ms);

#ifdef __cplusplus
}
#endif
