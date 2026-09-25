# 19 – MQTT client on the S3

TODO section 19. Target: **ESP32-S3** on the LAN, with Mosquitto running on
the MacBook (`tools/mqtt/start_broker.sh`).

`components/rb_mqtt` wraps ESP-MQTT (`espressif/mqtt`, from the component registry):

| Requirement | Implementation |
|---|---|
| Broker address | menuconfig `RB_BROKER_HOST` / `RB_BROKER_PORT` (never hard-coded) |
| Connect | started once the network has an IP |
| Detect disconnect | `MQTT_EVENT_DISCONNECTED` plus keep-alive (`RB_MQTT_KEEPALIVE_S`) |
| Automatic reconnect | every `RB_MQTT_RECONNECT_MS` |
| Connection state | `DISCONNECTED` / `CONNECTING` / `CONNECTED`, logged on change |
| Presence on the broker | retained `controller_status` message (`"online": true`) on `<prefix>/controller/status`, with a Last Will saying `"online": false` |

The MQTT state is kept completely separate from the safety state. Publishing
never happens on the safety task. It uses `esp_mqtt_client_enqueue`, so the
network I/O runs in the MQTT client's own task.

Test: `tools/mqtt/watch.sh` on the MacBook shows the heartbeat. Stop the
broker, wait for `broker disconnected`, start it again, and the client
reconnects without a reboot.

## Testing without hardware (QEMU)

Choose `RB_NET_QEMU_OPENETH` (menuconfig → *Network* → *Controller network
interface*). QEMU then emulates an Ethernet card whose host is reachable at
`10.0.2.2`:

```bash
printf 'CONFIG_RB_NET_QEMU_OPENETH=y\nCONFIG_RB_BROKER_HOST="10.0.2.2"\n' > /tmp/qemu.defaults
idf.py -B build_qemu -DSDKCONFIG=build_qemu/sdkconfig \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;/tmp/qemu.defaults" \
       qemu --qemu-extra-args="-nic user,model=open_eth"
```

This was run with Mosquitto on the host. The client connected, published the
retained status and heartbeats, detected the broker being stopped, dropped
QoS 0 messages while disconnected, and reconnected on its own after the
broker restarted.
