# Open questions (TODO section 32)

None of these has been answered silently. Each is either a configuration
option with a provisional default, or a hardware fact still to be checked
on the bench. Defaults are flagged `UNCONFIRMED` / `OPEN QUESTION` /
`UNVERIFIED` in menuconfig and in [configuration.md](configuration.md).

| Question | Status in this repository | Where to change it |
|---|---|---|
| Exact C4002 API/library | **Answered from DFRobot's official library**: UART 115200 8N1, framed protocol (`components/c4002/include/c4002_proto.h`). Needs confirming on hardware. | – |
| Exact C4002 UART pins | Placeholder GPIO17 (RX) / GPIO16 (TX) | `RB_C4002_RX_GPIO`, `RB_C4002_TX_GPIO` |
| Exact MLX90640 I2C pins | Placeholder GPIO19 (SDA) / GPIO20 (SCL), FireBeetle silkscreen | `RB_MLX_SDA_GPIO`, `RB_MLX_SCL_GPIO` |
| Exact S3 ESP-NOW/Wi-Fi configuration | Fixed channel (default 1) with Ethernet; with Wi-Fi it follows the AP channel (warning logged) | `RB_ESPNOW_CHANNEL`, `RB_NET_TYPE` |
| Waveshare buzzer interface | Default GPIO46 active-high (legacy code); active or passive selectable | `RB_BUZZER_GPIO`, `RB_BUZZER_DRIVE` |
| Waveshare relay interface | TCA9554 @0x20 on I2C SDA42/SCL41 (legacy code) | `RB_S3_TCA9554_ADDRESS`, `RB_S3_I2C_*` |
| Relay polarity | Default "energise to shut down"; fail-safe "de-energise to shut down" available | `RB_SHUTDOWN_POLARITY`, `RB_RELAY_ACTIVE_LEVEL` |
| Safe relay state during boot | Default released; glitch-free init either way | `RB_SHUTDOWN_BOOT_STATE` |
| RTC implementation/details | PCF85063 @0x51, stores UTC; SNTP writes back | `RB_S3_PCF85063_ADDRESS`, `RB_TIME_TZ` |
| Final cooking temperature threshold | Placeholder 50 °C on / 40 °C off | `RB_SAFETY_HEAT_ON_DC`, `RB_SAFETY_HEAT_OFF_DC` |
| Final temperature derivative algorithm | Rate over a sliding window (default 30 s); rate trigger off by default; `timing_hook` ready for trend-based timing | `RB_THERMAL_RATE_WINDOW_S`, `RB_SAFETY_HEAT_ON_RATE_DC_PER_MIN` |
| Presence debounce/filtering requirements | 2 s absence debounce, no return debounce; C4002 disappear delay 1 s | `RB_SAFETY_ABSENCE_DEBOUNCE_MS`, `RB_SAFETY_PRESENCE_RETURN_DEBOUNCE_MS`, `RB_C4002_DISAPPEAR_DELAY_S` |
| Exact warning timing | 60 s | `RB_SAFETY_WARNING_TIMEOUT_S` |
| Exact shutdown timing | 90 s, counted from the start of UNATTENDED (total) | `RB_SAFETY_SHUTDOWN_TIMEOUT_S`, `RB_SAFETY_SHUTDOWN_TIMING` |
| Behaviour if person returns during WARNING | Back to MONITORING; "require acknowledgement" available | `RB_SAFETY_WARNING_EXIT` |
| Behaviour if temperature falls while unattended | Keep the timers running; "return to IDLE" available | `RB_SAFETY_COOLING` |
| Behaviour when a safety-critical sensor fails | FAULT (buzzer fault pattern); the unattended timeline keeps counting; SHUTDOWN after 90 s in FAULT | `RB_SAFETY_FAULT_SHUTDOWN_S` |
| Ethernet or Wi-Fi for S3 → MQTT | Ethernet (W5500) by default; Wi-Fi supported | `RB_NET_TYPE` |
