#include "rb_net.h"

#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "rb_config.h"
#include "rb_wifi.h"
#if CONFIG_RB_NET_USES_ETHERNET || CONFIG_RB_NET_QEMU_OPENETH
#include "esp_eth.h"
#endif
#if CONFIG_RB_NET_USES_ETHERNET
#include "esp_eth_mac_w5500.h"
#include "esp_eth_phy_w5500.h"
#endif
#if CONFIG_RB_NET_QEMU_OPENETH
#include "esp_eth_mac_openeth.h"
#endif

static const char *TAG = "NET";

/* One network interface. DISABLED while it is not in use. */
typedef struct {
    const char *name;
    rb_net_state_t state;
    esp_ip4_addr_t ip;
} iface_t;

static iface_t s_eth = {.name = "Ethernet", .state = RB_NET_DISABLED};
static iface_t s_wifi = {.name = "Wi-Fi", .state = RB_NET_DISABLED};
static const iface_t *s_active; /* the interface rb_net_state() reports */
static rb_net_state_t s_state = RB_NET_DISABLED;
static rb_net_state_cb_t s_cb;
static void *s_cb_ctx;

static void iface_set(iface_t *iface, rb_net_state_t state)
{
    iface->state = state;
    if (state != RB_NET_CONNECTED) {
        iface->ip.addr = 0;
    }
}

/* Report Ethernet while it has an IP address, otherwise Wi-Fi if it is in use. */
static void update(void)
{
    const iface_t *active = s_eth.state == RB_NET_CONNECTED || s_wifi.state == RB_NET_DISABLED ? &s_eth : &s_wifi;
    if (active == s_active && active->state == s_state) {
        return;
    }
    s_active = active;
    s_state = active->state;
    if (s_cb != NULL) {
        s_cb(s_state, s_cb_ctx);
    }
}

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data);

#if CONFIG_RB_NET_ETHERNET_WIFI
static void ethernet_changed(void);
#else
static void ethernet_changed(void)
{
}
#endif

#if CONFIG_RB_NET_USES_ETHERNET || CONFIG_RB_NET_QEMU_OPENETH

static void on_eth_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Ethernet link up");
        iface_set(&s_eth, RB_NET_LINK_UP);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Ethernet link down");
        iface_set(&s_eth, RB_NET_DOWN);
        break;
    default:
        return;
    }
    ethernet_changed();
    update();
}

/* Install the Ethernet driver, attach it to a DHCP netif and start it. */
static esp_err_t attach_ethernet(esp_eth_mac_t *mac, esp_eth_phy_t *phy, const char *name)
{
    ESP_RETURN_ON_FALSE(mac && phy, ESP_FAIL, TAG, "%s driver", name);
    const esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth = NULL;
    ESP_RETURN_ON_ERROR(esp_eth_driver_install(&eth_cfg, &eth), TAG, "%s not responding (check SPI pins)", name);

    /* The W5500 has no factory MAC address: use the one reserved for Ethernet in eFuse. */
    uint8_t mac_addr[6];
    ESP_RETURN_ON_ERROR(esp_read_mac(mac_addr, ESP_MAC_ETH), TAG, "MAC");
    ESP_RETURN_ON_ERROR(esp_eth_ioctl(eth, ETH_CMD_S_MAC_ADDR, mac_addr), TAG, "set MAC");

    /* Above Wi-Fi STA (100): while both have an address, traffic goes over Ethernet. */
    esp_netif_inherent_config_t netif_base = ESP_NETIF_INHERENT_DEFAULT_ETH();
    netif_base.route_prio = 150;
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    netif_cfg.base = &netif_base;
    esp_netif_t *netif = esp_netif_new(&netif_cfg);
    ESP_RETURN_ON_ERROR(esp_netif_attach(netif, esp_eth_new_netif_glue(eth)), TAG, "netif");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, on_eth_event, NULL), TAG, "eth events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_got_ip, &s_eth), TAG,
                        "ip events");
    iface_set(&s_eth, RB_NET_DOWN);
    update();
    ESP_LOGI(TAG, "%s Ethernet started, MAC " MACSTR, name, MAC2STR(mac_addr));
    /* Link loss and DHCP renewal are handled by esp_eth / esp_netif. */
    return esp_eth_start(eth);
}

#endif

#if CONFIG_RB_NET_QEMU_OPENETH

static esp_err_t start_qemu_openeth(void)
{
    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.autonego_timeout_ms = 100;
    ESP_LOGW(TAG, "using QEMU emulated Ethernet: test builds only");
    return attach_ethernet(esp_eth_mac_new_openeth(&mac_cfg), esp_eth_phy_new_generic(&phy_cfg), "QEMU OpenETH");
}

#endif

#if CONFIG_RB_NET_USES_ETHERNET

static esp_err_t start_ethernet(void)
{
    /* The W5500 driver needs the GPIO ISR service; it may already be installed. */
    esp_err_t isr = gpio_install_isr_service(0);
    ESP_RETURN_ON_FALSE(isr == ESP_OK || isr == ESP_ERR_INVALID_STATE, isr, TAG, "isr service");
    const spi_host_device_t host = (spi_host_device_t)CONFIG_RB_ETH_SPI_HOST;
    const spi_bus_config_t bus = {
        .mosi_io_num = CONFIG_RB_ETH_MOSI_GPIO,
        .miso_io_num = CONFIG_RB_ETH_MISO_GPIO,
        .sclk_io_num = CONFIG_RB_ETH_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(host, &bus, SPI_DMA_CH_AUTO), TAG, "SPI bus");

    spi_device_interface_config_t devcfg = {
        .mode = 0,
        .clock_speed_hz = CONFIG_RB_ETH_SPI_MHZ * 1000 * 1000,
        .queue_size = 20,
        .spics_io_num = CONFIG_RB_ETH_CS_GPIO,
    };
    eth_w5500_config_t w5500 = ETH_W5500_DEFAULT_CONFIG(host, &devcfg);
    w5500.base.int_gpio_num = CONFIG_RB_ETH_INT_GPIO;
    w5500.base.poll_period_ms = CONFIG_RB_ETH_INT_GPIO < 0 ? CONFIG_RB_ETH_POLL_MS : 0;

    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.reset_gpio_num = CONFIG_RB_ETH_RST_GPIO;
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500, &mac_cfg);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_cfg);
    ESP_RETURN_ON_FALSE(mac && phy, ESP_FAIL, TAG, "W5500 driver");

    return attach_ethernet(mac, phy, "W5500");
}

#endif

#if CONFIG_RB_NET_USES_WIFI

#define CHANNEL_WATCH_MS 1000

static esp_timer_handle_t s_retry_timer;
static esp_timer_handle_t s_channel_timer;
static uint32_t s_backoff_ms = 1000;
static uint8_t s_channel = CONFIG_RB_ESPNOW_CHANNEL;

static void retry_connect(void *arg)
{
    esp_wifi_connect();
}

/*
 * The access point sets the channel, and ESP-NOW goes with it. The nodes
 * find the new channel on their own; this only reports it. Checked while
 * associated, because the radio hops between channels while (re)connecting.
 */
static void watch_channel(void *arg)
{
    if (s_wifi.state != RB_NET_LINK_UP && s_wifi.state != RB_NET_CONNECTED) {
        return;
    }
    const uint8_t channel = rb_wifi_get_channel();
    if (channel != 0 && channel != s_channel) {
        ESP_LOGW(TAG, "ESP-NOW channel %u -> %u (set by the access point); the nodes follow it", s_channel, channel);
        s_channel = channel;
    }
}

/* Back to RB_ESPNOW_CHANNEL once Wi-Fi is no longer in use. */
static void restore_channel(void)
{
    const uint8_t channel = rb_wifi_get_channel();
    if (channel != CONFIG_RB_ESPNOW_CHANNEL) {
        if (rb_wifi_set_channel(CONFIG_RB_ESPNOW_CHANNEL) != ESP_OK) {
            return; /* still associated: retried on the disconnect event */
        }
        ESP_LOGW(TAG, "ESP-NOW channel %u -> %d (Wi-Fi no longer in use); the nodes follow it", channel,
                 CONFIG_RB_ESPNOW_CHANNEL);
    }
    s_channel = CONFIG_RB_ESPNOW_CHANNEL;
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (s_wifi.state == RB_NET_DISABLED) {
        /* Left on purpose (Ethernet is back): no retry. */
        if (id == WIFI_EVENT_STA_DISCONNECTED) {
            restore_channel();
        }
        return;
    }
    if (id == WIFI_EVENT_STA_CONNECTED) {
        s_backoff_ms = 1000;
        ESP_LOGI(TAG, "Wi-Fi associated, channel %u", rb_wifi_get_channel());
        iface_set(&s_wifi, RB_NET_LINK_UP);
        watch_channel(NULL);
        update();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        iface_set(&s_wifi, RB_NET_DOWN);
        update();
        ESP_LOGW(TAG, "Wi-Fi disconnected; retrying in %lu ms", (unsigned long)s_backoff_ms);
        esp_timer_stop(s_retry_timer);
        esp_timer_start_once(s_retry_timer, (uint64_t)s_backoff_ms * 1000);
        /* 1, 2, 4 ... up to the longest, then start over: a slow router is retried soon again. */
        s_backoff_ms = s_backoff_ms * 2 > CONFIG_RB_WIFI_MAX_BACKOFF_MS ? 1000 : s_backoff_ms * 2;
    }
}

/* Station configured, not connecting yet. */
static esp_err_t wifi_setup(void)
{
    ESP_RETURN_ON_FALSE(CONFIG_RB_WIFI_SSID[0] != '\0', ESP_ERR_INVALID_ARG, TAG, "RB_WIFI_SSID not set");
    ESP_RETURN_ON_ERROR(rb_wifi_base_start(), TAG, "wifi");
    const esp_timer_create_args_t targs = {.callback = retry_connect, .name = "wifi_retry"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&targs, &s_retry_timer), TAG, "timer");
    const esp_timer_create_args_t wargs = {.callback = watch_channel, .name = "wifi_channel"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&wargs, &s_channel_timer), TAG, "timer");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_channel_timer, CHANNEL_WATCH_MS * 1000), TAG, "timer");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL), TAG, "events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_got_ip, &s_wifi), TAG, "ip");

    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid, CONFIG_RB_WIFI_SSID, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, CONFIG_RB_WIFI_PASSWORD, sizeof(cfg.sta.password));
    return esp_wifi_set_config(WIFI_IF_STA, &cfg);
}

static void wifi_join(void)
{
    s_backoff_ms = 1000;
    iface_set(&s_wifi, RB_NET_DOWN);
    ESP_LOGI(TAG, "connecting to Wi-Fi '%s'", CONFIG_RB_WIFI_SSID);
    esp_wifi_connect();
}

#endif

#if CONFIG_RB_NET_ETHERNET_WIFI

/*
 * Ethernet preferred, Wi-Fi as the fallback. Every decision runs in the
 * default event loop task, so the fallback timer only posts an event.
 */
ESP_EVENT_DEFINE_BASE(RB_NET_EVENT);
enum { RB_NET_EVENT_FALLBACK };

static esp_timer_handle_t s_fallback_timer;

static void fallback_due(void *arg)
{
    esp_event_post(RB_NET_EVENT, RB_NET_EVENT_FALLBACK, NULL, 0, 0);
}

static void wifi_leave(void)
{
    iface_set(&s_wifi, RB_NET_DISABLED);
    esp_timer_stop(s_retry_timer);
    esp_wifi_disconnect();
    restore_channel();
}

static void ethernet_changed(void)
{
    if (s_fallback_timer == NULL) {
        return; /* no Wi-Fi configured */
    }
    if (s_eth.state == RB_NET_CONNECTED) {
        esp_timer_stop(s_fallback_timer);
        if (s_wifi.state != RB_NET_DISABLED) {
            ESP_LOGI(TAG, "Ethernet is back: leaving Wi-Fi");
            wifi_leave();
        }
    } else if (s_wifi.state == RB_NET_DISABLED && !esp_timer_is_active(s_fallback_timer)) {
        esp_timer_start_once(s_fallback_timer, (uint64_t)CONFIG_RB_NET_FALLBACK_S * 1000 * 1000);
    }
}

static void on_fallback(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (s_eth.state == RB_NET_CONNECTED || s_wifi.state != RB_NET_DISABLED) {
        return;
    }
    ESP_LOGW(TAG, "Ethernet has no IP address: switching to Wi-Fi");
    wifi_join();
    update();
}

static esp_err_t start_ethernet_wifi(void)
{
    if (CONFIG_RB_WIFI_SSID[0] == '\0') {
        ESP_LOGW(TAG, "RB_WIFI_SSID not set: Ethernet only, no Wi-Fi fallback");
        return start_ethernet();
    }
    ESP_RETURN_ON_ERROR(wifi_setup(), TAG, "Wi-Fi");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(RB_NET_EVENT, RB_NET_EVENT_FALLBACK, on_fallback, NULL), TAG,
                        "events");
    const esp_timer_create_args_t args = {.callback = fallback_due, .name = "net_fallback"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&args, &s_fallback_timer), TAG, "timer");
    /* Counted from boot: Ethernet gets this long to come up before Wi-Fi is tried. */
    ESP_RETURN_ON_ERROR(esp_timer_start_once(s_fallback_timer, (uint64_t)CONFIG_RB_NET_FALLBACK_S * 1000 * 1000),
                        TAG, "timer");
    const esp_err_t err = start_ethernet();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet not started (%s): using Wi-Fi", esp_err_to_name(err));
        esp_timer_stop(s_fallback_timer);
        fallback_due(NULL);
    }
    return ESP_OK;
}

#endif

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    iface_t *iface = arg;
    if (iface->state == RB_NET_DISABLED) {
        return; /* Wi-Fi already left */
    }
    const ip_event_got_ip_t *event = data;
    iface_set(iface, RB_NET_CONNECTED);
    iface->ip = event->ip_info.ip;
    ESP_LOGI(TAG, "%s connected: IP " IPSTR " gateway " IPSTR, iface->name, IP2STR(&event->ip_info.ip),
             IP2STR(&event->ip_info.gw));
    if (iface == &s_eth) {
        ethernet_changed();
    }
    update();
}

esp_err_t rb_net_start(rb_net_state_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_cb_ctx = ctx;
#if CONFIG_RB_NET_NONE
    ESP_LOGW(TAG, "network disabled: local safety only, no telemetry");
    return ESP_OK;
#else
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    esp_err_t err = esp_event_loop_create_default();
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "event loop");
#if CONFIG_RB_NET_ETHERNET
    return start_ethernet();
#elif CONFIG_RB_NET_QEMU_OPENETH
    return start_qemu_openeth();
#elif CONFIG_RB_NET_WIFI
    ESP_RETURN_ON_ERROR(wifi_setup(), TAG, "Wi-Fi");
    wifi_join();
    update();
    return ESP_OK;
#else
    return start_ethernet_wifi();
#endif
#endif
}

rb_net_state_t rb_net_state(void)
{
    return s_state;
}

const char *rb_net_state_name(rb_net_state_t state)
{
    static const char *const names[] = {"DISABLED", "DOWN", "LINK_UP", "CONNECTED"};
    return (unsigned)state < sizeof(names) / sizeof(names[0]) ? names[state] : "?";
}

const char *rb_net_interface_name(void)
{
    return s_active != NULL ? s_active->name : "none";
}

esp_ip4_addr_t rb_net_ip(void)
{
    return s_active != NULL ? s_active->ip : (esp_ip4_addr_t){0};
}
