# 08 – Sensor-node health monitoring

TODO section 8. Target: **ESP32-S3**, paired with project 05 on the C6.

```text
NEVER_SEEN → ONLINE → STALE → OFFLINE
               ↑________|_________|   (any accepted packet)
```

| Setting (menuconfig → *Controller board* → *Sensor node link*) | Default |
|---|---|
| `RB_CTRL_NODE_STALE_MS` | 2000 ms |
| `RB_CTRL_NODE_OFFLINE_MS` | 10000 ms |

Events (`node_event_t`) returned by `sensor_node_evaluate()`:

| Kind | Events |
|---|---|
| Fault | `NODE_EVT_STALE`, `NODE_EVT_OFFLINE`, `NODE_EVT_PRESENCE_INVALID`, `NODE_EVT_THERMAL_INVALID` |
| Recovery | `NODE_EVT_ONLINE`, `NODE_EVT_PRESENCE_VALID`, `NODE_EVT_THERMAL_VALID` |

Missing packets are detected in two ways: by time (STALE/OFFLINE) and by
sequence gaps (`missed` counter).

**Missing data is not safe.** `sensor_node_inputs()` only reports presence
`PRESENT` or `ABSENT` when the node is ONLINE, the data is fresh and the C4002
reading is valid. In every other case it reports `UNKNOWN`. The safety state
machine (section 9) never treats `UNKNOWN` as "nobody there".
