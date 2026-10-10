#include "rb_node_id.h"

#include <stdbool.h>
#include <stdio.h>

#ifndef RB_NODE_ID_HOST_TEST
#include "esp_log.h"
#include "esp_mac.h"
#include "rb_config.h"

static const char *TAG = "NODE";
#endif

uint32_t rb_node_id_lookup(const char *map, const uint8_t mac[6], uint32_t fallback)
{
    const char *p = map;
    while (p != NULL && *p != '\0') {
        while (*p == ',' || *p == ' ') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        unsigned m[6];
        unsigned long id;
        int used = 0;
        if (sscanf(p, "%2x:%2x:%2x:%2x:%2x:%2x=%lu%n", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5], &id, &used) != 7 ||
            id == 0 || id > 65535) {
            return fallback; /* malformed: don't guess */
        }
        bool match = true;
        for (int i = 0; i < 6; i++) {
            match = match && m[i] == mac[i];
        }
        if (match) {
            return (uint32_t)id;
        }
        p += used;
    }
    return fallback;
}

#ifndef RB_NODE_ID_HOST_TEST
uint32_t rb_node_id(void)
{
    static uint32_t s_id;
    if (s_id == 0) {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        s_id = rb_node_id_lookup(CONFIG_RB_NODE_ID_BY_MAC, mac, 0);
        if (s_id != 0) {
            ESP_LOGI(TAG, "node ID %lu from RB_NODE_ID_BY_MAC (MAC " MACSTR ")", (unsigned long)s_id, MAC2STR(mac));
        } else {
            s_id = CONFIG_RB_NODE_ID;
            ESP_LOGI(TAG, "node ID %lu from RB_NODE_ID (MAC " MACSTR " not in RB_NODE_ID_BY_MAC)",
                     (unsigned long)s_id, MAC2STR(mac));
        }
    }
    return s_id;
}
#endif
