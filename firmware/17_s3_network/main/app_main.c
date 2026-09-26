/*
 * TODO section 17: S3 network connectivity.
 *
 * Brings up the configured interface (W5500 Ethernet by default), logs
 * every state change (link, DHCP address, loss), and once connected pings
 * the MacBook (RB_BROKER_HOST) every 10 s to check the LAN path.
 * Reconnection is automatic: unplug and replug the cable (or turn the
 * access point off and on) and watch the log.
 */
#include <inttypes.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "ping/ping_sock.h"
#include "rb_config.h"
#include "rb_net.h"

static const char *TAG = "NET";

#define PING_PERIOD_MS 10000

static void on_state(rb_net_state_t state, void *ctx)
{
    if (state == RB_NET_CONNECTED) {
        ESP_LOGI(TAG, "network state: %s", rb_net_state_name(state));
    } else {
        ESP_LOGW(TAG, "network state: %s", rb_net_state_name(state));
    }
}

static void on_ping_end(esp_ping_handle_t hdl, void *args)
{
    uint32_t sent = 0, received = 0, duration = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &sent, sizeof(sent));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &duration, sizeof(duration));
    if (received > 0) {
        ESP_LOGI(TAG, "MacBook %s reachable: %" PRIu32 "/%" PRIu32 " replies in %" PRIu32 " ms", CONFIG_RB_BROKER_HOST,
                 received, sent, duration);
    } else {
        ESP_LOGW(TAG, "MacBook %s NOT reachable (0/%" PRIu32 " replies)", CONFIG_RB_BROKER_HOST, sent);
    }
    esp_ping_delete_session(hdl);
}

static void ping_macbook(void)
{
    struct addrinfo hints = {.ai_family = AF_INET};
    struct addrinfo *res = NULL;
    if (getaddrinfo(CONFIG_RB_BROKER_HOST, NULL, &hints, &res) != 0 || res == NULL) {
        ESP_LOGW(TAG, "cannot resolve '%s'", CONFIG_RB_BROKER_HOST);
        return;
    }
    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    inet_addr_to_ip4addr(ip_2_ip4(&cfg.target_addr), &((struct sockaddr_in *)res->ai_addr)->sin_addr);
    cfg.target_addr.type = IPADDR_TYPE_V4;
    cfg.count = 3;
    freeaddrinfo(res);
    const esp_ping_callbacks_t cbs = {.on_ping_end = on_ping_end};
    esp_ping_handle_t ping;
    if (esp_ping_new_session(&cfg, &cbs, &ping) == ESP_OK) {
        esp_ping_start(ping);
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(rb_net_start(on_state, NULL));
    if (CONFIG_RB_BROKER_HOST[0] == '\0') {
        ESP_LOGW(TAG, "RB_BROKER_HOST not set: skipping the MacBook reachability check");
    }
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(PING_PERIOD_MS));
        const esp_ip4_addr_t ip = rb_net_ip();
        ESP_LOGI(TAG, "%s %s, IP " IPSTR, rb_net_interface_name(), rb_net_state_name(rb_net_state()), IP2STR(&ip));
        if (rb_net_state() == RB_NET_CONNECTED && CONFIG_RB_BROKER_HOST[0] != '\0') {
            ping_macbook();
        }
    }
}
