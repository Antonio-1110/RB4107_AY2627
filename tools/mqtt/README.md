# MacBook MQTT broker (Mosquitto)

TODO section 18. This runs on the MacBook, not on an ESP32.

```text
ESP32-S3 ──Ethernet──▶ <MacBook LAN IP>:1883 ──▶ Mosquitto ──▶ Django subscriber
```

## 1. Install

```bash
brew install mosquitto
```

## 2. Configure

[`mosquitto.conf`](mosquitto.conf) is ready to use. It listens on `0.0.0.0:1883`,
because Mosquitto 2.x only listens on localhost unless a `listener` is set,
and allows anonymous clients (a LAN bench setup; see the note in the file).

## 3. Start

```bash
./start_broker.sh          # foreground, logs connections; Ctrl-C to stop
```

The first time, macOS may ask whether *mosquitto* may accept incoming
connections. Click **Allow**; otherwise the S3 can't connect.

(`brew services start mosquitto` runs Homebrew's default config, which is
localhost-only. Use this script instead.)

## 4. Find the MacBook's LAN IP

```bash
./lan_ip.sh                # e.g. 192.168.1.127
```

Enter that value in the S3 firmware: `idf.py menuconfig` → *RB4107
configuration* → *MQTT broker (MacBook)* → `RB_BROKER_HOST`. It is not
hard-coded anywhere. If the MacBook's IP changes, use a DHCP reservation or
its `.local` hostname.

## 5. Verify

| Check | Command | Expected |
|---|---|---|
| Port 1883 listening on all interfaces | `./check_port.sh` | `*:1883 (LISTEN)` |
| Local publish/subscribe | `./test_pubsub.sh` | `PASS: localhost:1883 ...` |
| From another LAN device | `./test_pubsub.sh <MacBook IP>` (run on the other device) | `PASS: <ip>:1883 ...` |
| Watch RB4107 traffic | `./watch.sh` | `rb4107/...` topics once the S3 publishes |

Both checks were run with Mosquitto 2.0.18 on Linux while writing these
scripts. They still need to be repeated on the MacBook.
