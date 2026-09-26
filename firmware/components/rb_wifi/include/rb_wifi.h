#pragma once

/*
 * Common Wi-Fi bring-up. ESP-NOW needs the Wi-Fi driver running even when
 * the controller uses Ethernet, and the Wi-Fi network option (section 17)
 * needs the same setup. This does it once, whoever asks first.
 */
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* NVS, netif, default event loop and Wi-Fi STA started. Safe to call more than once. */
esp_err_t rb_wifi_base_start(void);

/* Fix the radio channel (only when not associated with an AP). */
esp_err_t rb_wifi_set_channel(uint8_t channel);

/* Current primary channel. */
uint8_t rb_wifi_get_channel(void);

#ifdef __cplusplus
}
#endif
