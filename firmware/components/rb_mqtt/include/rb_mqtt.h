#pragma once

/*
 * MQTT client on the S3 (TODO section 19), a thin wrapper over ESP-MQTT.
 *
 *   DISCONNECTED -> CONNECTING -> CONNECTED
 *
 * - Reconnects automatically every RB_MQTT_RECONNECT_MS.
 * - The broker publishes a retained Last Will ("offline") on the status topic
 *   if the controller disappears; "online" is published on every connect.
 * - Its state is completely separate from the safety state. Only the
 *   telemetry task publishes; the safety task never does, so an MQTT stall
 *   can't hold up safety processing.
 */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RB_MQTT_DISCONNECTED = 0,
    RB_MQTT_CONNECTING,
    RB_MQTT_CONNECTED,
} rb_mqtt_state_t;

typedef void (*rb_mqtt_state_cb_t)(rb_mqtt_state_t state, void *ctx);

typedef struct {
    char uri[96];              /* mqtt://host:port */
    const char *client_id;
    const char *status_topic;  /* retained online/offline status (Last Will) */
    const char *online_payload;
    const char *offline_payload;
} rb_mqtt_config_t;

typedef struct {
    uint32_t published;        /* accepted by the client */
    uint32_t dropped;          /* QoS 0 while disconnected, or outbox full */
    uint32_t connects;
    uint32_t disconnects;
} rb_mqtt_stats_t;

/* URI and client ID from menuconfig; the caller fills in the topics and payloads. */
rb_mqtt_config_t rb_mqtt_config_from_kconfig(void);

/* Create the client and start connecting (retrying in the background). */
esp_err_t rb_mqtt_start(const rb_mqtt_config_t *config, rb_mqtt_state_cb_t cb, void *ctx);

/*
 * Publish a message. QoS 0 is dropped while disconnected (stale telemetry is
 * worthless); QoS 1 is kept in the bounded outbox and sent after reconnect.
 * Can block for the network timeout while the link is bad, so call it from
 * the telemetry task only, never from the safety task.
 */
esp_err_t rb_mqtt_publish(const char *topic, const char *payload, int qos, bool retain);

rb_mqtt_state_t rb_mqtt_state(void);
const char *rb_mqtt_state_name(rb_mqtt_state_t state);
void rb_mqtt_get_stats(rb_mqtt_stats_t *out);

#ifdef __cplusplus
}
#endif
