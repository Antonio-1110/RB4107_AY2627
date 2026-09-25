# 20 – MQTT topic structure

TODO section 20. Target: **ESP32-S3** (or QEMU; see project 19's README).

The topic tree is defined once, in `components/rb_topics`, and documented in
[`docs/mqtt_topics.md`](../../docs/mqtt_topics.md). The prefix is
configurable (`RB_MQTT_TOPIC_PREFIX`, default `rb4107`).

This project prints the tree with each topic's QoS and retain policy, then
publishes a test message to every topic. Watch them arrive with
`tools/mqtt/watch.sh`. It was run in QEMU against Mosquitto, and all nine
test topics plus the status topic arrived.
