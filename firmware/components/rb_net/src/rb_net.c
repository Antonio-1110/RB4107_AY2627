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
#if CONFIG_RB_NET_ETHERNET || CONFIG_RB_NET_QEMU_OPENETH
#include "esp_eth.h"
#endif
#if CONFIG_RB_NET_ETHERNET
#include "esp_eth_mac_w5500.h"
#include "esp_eth_phy_w5500.h"
#endif
#if CONFIG_RB_NET_QEMU_OPENETH
#include "esp_eth_mac_openeth.h"
#endif

static const char *TAG = "NET";

static rb_net_state_t s_state = RB_NET_DISABLED;
static esp_ip4_addr_t s_ip;
static rb_net_state_cb_t s_cb;
static void *s_cb_ctx;

static void set_state(rb_net_state_t state)
{
    if (state == s_state) {
        return;
    }
    s_state = state;
    if (state != RB_NET_CONNECTED) {
        s_ip.addr = 0;
    }
    if (s_cb != NULL) {
        s_cb(state, s_cb_ctx);
    }
}

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const ip_event_got_ip_t *event = data;
    s_ip = event->ip_info.ip;
    ESP_LOGI(TAG, "%s connected: IP " IPSTR " gateway " IPSTR, rb_net_interface_name(), IP2STR(&event->ip_info.ip),
             IP2STR(&event->ip_info.gw));
    set_state(RB_NET_CONNECTED);
}

#if CONFIG_RB_NET_ETHERNET || CONFIG_RB_NET_QEMU_OPENETH

static void on_eth_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Ethernet link up");
        set_state(RB_NET_LINK_UP);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Ethernet link down");
        set_state(RB_NET_DOWN);
        break;
    default:
        break;
    }
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

    const esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *netif = esp_netif_new(&netif_cfg);
    ESP_RETURN_ON_ERROR(esp_netif_attach(netif, esp_eth_new_netif_glue(eth)), TAG, "netif");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, on_eth_event, NULL), TAG, "eth events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_got_ip, NULL), TAG, "ip events");
    set_state(RB_NET_DOWN);
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

#if CONFIG_RB_NET_ETHERNET

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

#elif CONFIG_RB_NET_WIFI

static esp_timer_handle_t s_retry_timer;
static uint32_t s_backoff_ms = 1000;

static void retry_connect(void *arg)
{
    esp_wifi_connect();
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_CONNECTED) {
        s_backoff_ms = 1000;
        ESP_LOGI(TAG, "Wi-Fi associated, channel %u", rb_wifi_get_channel());
        if (rb_wifi_get_channel() != CONFIG_RB_ESPNOW_CHANNEL) {
            ESP_LOGW(TAG, "access point is on channel %u but RB_ESPNOW_CHANNEL is %d: ESP-NOW now uses channel %u, "
                          "so every sensor node must match", rb_wifi_get_channel(), CONFIG_RB_ESPNOW_CHANNEL,
                     rb_wifi_get_channel());
        }
        set_state(RB_NET_LINK_UP);
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        set_state(RB_NET_DOWN);
        ESP_LOGW(TAG, "Wi-Fi disconnected; retrying in %lu ms", (unsigned long)s_backoff_ms);
        esp_timer_stop(s_retry_timer);
        esp_timer_start_once(s_retry_timer, (uint64_t)s_backoff_ms * 1000);
        s_backoff_ms = s_backoff_ms * 2 > CONFIG_RB_WIFI_MAX_BACKOFF_MS ? CONFIG_RB_WIFI_MAX_BACKOFF_MS : s_backoff_ms * 2;
    }
}

static esp_err_t start_wifi(void)
{
    ESP_RETURN_ON_FALSE(CONFIG_RB_WIFI_SSID[0] != '\0', ESP_ERR_INVALID_ARG, TAG, "RB_WIFI_SSID not set");
    ESP_RETURN_ON_ERROR(rb_wifi_base_start(), TAG, "wifi");
    const esp_timer_create_args_t targs = {.callback = retry_connect, .name = "wifi_retry"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&targs, &s_retry_timer), TAG, "timer");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL), TAG, "events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_got_ip, NULL), TAG, "ip");

    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid, CONFIG_RB_WIFI_SSID, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, CONFIG_RB_WIFI_PASSWORD, sizeof(cfg.sta.password));
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG, "config");
    set_state(RB_NET_DOWN);
    ESP_LOGI(TAG, "connecting to Wi-Fi '%s'", CONFIG_RB_WIFI_SSID);
    return esp_wifi_connect();
}

#endif

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
#else
    return start_wifi();
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
#if CONFIG_RB_NET_ETHERNET || CONFIG_RB_NET_QEMU_OPENETH
    return "Ethernet";
#elif CONFIG_RB_NET_WIFI
    return "Wi-Fi";
#else
    return "none";
#endif
}

esp_ip4_addr_t rb_net_ip(void)
{
    return s_ip;
}
