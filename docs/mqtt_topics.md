# MQTT topics

Defined in [`firmware/components/rb_topics`](../firmware/components/rb_topics).
The prefix `rb4107` is configurable (`RB_MQTT_TOPIC_PREFIX`). Node names are
`node_<id>` with the ID zero-padded to two digits (`node_01`).

```text
rb4107/
├── controller/
│   ├── status      retained online/offline (Last Will)
│   ├── heartbeat   liveness
│   ├── state       full telemetry snapshot (schema in mqtt_schema.md)
│   └── faults      active fault list
├── sensors/
│   └── <node>/
│       ├── presence
│       ├── thermal
│       └── status  node link state
└── events/
    ├── warning
    ├── shutdown
    └── fault
```

| Topic | QoS | Retained | When |
|---|---|---|---|
| `controller/status` | 1 | yes | on connect (`online: true`); Last Will (`online: false`) |
| `controller/heartbeat` | 0 | no | every telemetry period |
| `controller/state` | 0 | yes | every telemetry period, and right away on every state transition |
| `controller/faults` | 1 | yes | on every fault raised/cleared |
| `sensors/<node>/presence` | 0 | no | every telemetry period |
| `sensors/<node>/thermal` | 0 | no | every telemetry period |
| `sensors/<node>/status` | 1 | yes | on node ONLINE/STALE/OFFLINE and sensor validity changes |
| `events/warning` | 1 | no | entering WARNING |
| `events/shutdown` | 1 | no | entering SHUTDOWN |
| `events/fault` | 1 | no | fault raised or cleared; entering FAULT |

Retained topics let a subscriber that starts later (e.g. Django) see the
current status, state and faults straight away.

Subscribe to everything: `mosquitto_sub -h <broker> -t 'rb4107/#' -v`
(or `tools/mqtt/watch.sh`).
