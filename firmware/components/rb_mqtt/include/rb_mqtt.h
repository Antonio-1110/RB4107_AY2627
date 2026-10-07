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
 * - Incoming messages (remote C4002 tuning only) go to the callbacks
 *   registered with rb_mqtt_subscribe(), in the MQTT client's task.
 */
#include <stdbool.h>
#include <stddef.h>
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

/* An incoming message. topic is NUL-terminated; data is not. Runs in the MQTT task: don't block. */
typedef void (*rb_mqtt_rx_cb_t)(const char *topic, const char *data, size_t len, void *ctx);

typedef struct {
    char uri[96];              /* mqtt://host:port */
    char fallback_uri[96];     /* tried in turn with uri after a failed attempt; "" = none */
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
    uint32_t received;         /* messages handed to a subscription callback */
    uint32_t rx_dropped;       /* fragmented or with an over-long topic */
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

/*
 * Subscribe to filter (e.g. "rb4107/sensors/+/c4002_set") on every connect.
 * Call before rb_mqtt_start(). Messages larger than the client's receive
 * buffer arrive in pieces and are dropped.
 */
esp_err_t rb_mqtt_subscribe(const char *filter, int qos, rb_mqtt_rx_cb_t cb, void *ctx);

rb_mqtt_state_t rb_mqtt_state(void);
/* The broker URI in use (or tried next). */
const char *rb_mqtt_broker_uri(void);
const char *rb_mqtt_state_name(rb_mqtt_state_t state);
void rb_mqtt_get_stats(rb_mqtt_stats_t *out);

#ifdef __cplusplus
}
#endif
