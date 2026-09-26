#include "network.h"

#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "rtc.h"
#include "wifi_secrets.h"
#include "system_config.h"

static const char *TAG = "NETWORK";
static bool connected;

static void network_time_sync_callback(struct timeval *timestamp)
{
    if (timestamp != NULL)
    {
        (void)rtc_set_time(timestamp->tv_sec);
        ESP_LOGI(TAG, "SNTP time synchronized and written to RTC");
    }
}

static void network_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        (void)esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        connected = false;
        ESP_LOGW(TAG, "Wi-Fi disconnected; reconnecting");
        (void)esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        connected = true;
        ESP_LOGI(TAG, "Wi-Fi connected");
    }
}

esp_err_t network_init(void)
{
    if (!WIFI_ENABLE)
    {
        ESP_LOGW(TAG, "Wi-Fi is disabled");
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop init failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, network_event_handler, NULL), TAG, "Wi-Fi event registration failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, network_event_handler, NULL), TAG, "IP event registration failed");
    esp_netif_create_default_wifi_sta();

    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    sntp_config.sync_cb = network_time_sync_callback;
    ESP_RETURN_ON_ERROR(esp_netif_sntp_init(&sntp_config), TAG, "SNTP init failed");

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_config), TAG, "Wi-Fi init failed");
    wifi_config_t station_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_OPEN,
        },
    };
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Wi-Fi mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &station_config), TAG, "Wi-Fi config failed");
    return esp_wifi_start();
}

bool network_is_connected(void)
{
    return connected;
}