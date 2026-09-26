# 17 – S3 network connectivity

> **Test-only project, not part of the running system.** It checks one part on its own and replaces the system firmware on that board. To run the
> system, flash [`05a_c6_presence_node`](../05a_c6_presence_node) on the two
> presence C6 boards, [`05b_c6_thermal_node`](../05b_c6_thermal_node) on the
> thermal C6 board and [`29_end_to_end`](../29_end_to_end) on the S3.

TODO section 17. Target: **Waveshare ESP32-S3-ETH-8DI-8RO**.

```text
ESP32-S3 → W5500 Ethernet → local LAN → MacBook
```

`components/rb_net` sets up the on-board **W5500** SPI Ethernet (pins in
menuconfig → *Network* → *W5500 Ethernet*, taken from the legacy controller),
or a Wi-Fi station as the alternative (`RB_NET_TYPE`).

| Check | How |
|---|---|
| Obtain IP address | `NET: Ethernet connected: IP ...` (DHCP) |
| LAN connectivity / MacBook | set `RB_BROKER_HOST` to the MacBook IP. The project pings it every 10 s. |
| Log network state | every change: `DOWN` → `LINK_UP` → `CONNECTED` |
| Reconnect / recovery | Ethernet: esp_eth handles link loss and DHCP renewal. Wi-Fi: exponential back-off up to `RB_WIFI_MAX_BACKOFF_MS`. |

Wi-Fi note: with Wi-Fi the radio follows the access point's channel, so
ESP-NOW has to use that same channel on every sensor node. The log warns if
`RB_ESPNOW_CHANNEL` differs. This is one reason Ethernet is preferred.

## Dependencies

The W5500 driver moved out of ESP-IDF in v6.0. It comes from the component
registry (`espressif/w5500`, declared in `components/rb_net/idf_component.yml`)
and is downloaded on the first build.
