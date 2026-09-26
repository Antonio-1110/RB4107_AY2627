# Engineering rules (TODO section 33): where each one is enforced

| # | Rule | How the code enforces it |
|---|---|---|
| 1 | Safety logic runs locally on the ESP32-S3 | `components/safety` runs in the S3's safety task (`rb_controller`); the C6 only acquires and transmits |
| 2 | MQTT/Django never required for shutdown | the safety task never calls into MQTT; the telemetry path starts last and is best-effort (`rb_controller_app`); critical failure test (30) |
| 3 | No blocking delays in safety logic | state machine is `now - start >= timeout` only (`safety.c`); safety task waits at most one tick on its queue (11, 12) |
| 4 | Network failures don't block safety processing | events posted with zero timeout; snapshot lock only for a struct copy; publishing only in the telemetry task (22); verified in QEMU (30) |
| 5 | Missing sensor data ≠ safe reading | tri-state presence (`RB_UNKNOWN`), staleness timeouts on C6 and S3, UNKNOWN → FAULT, JSON `null` (8, 9, 21); tests in 28 |
| 6 | Hardware drivers separate from safety logic | the state machine returns outputs; `rb_outputs` drives `buzzer` / `shutdown_output` |
| 7 | Centralised configuration | one Kconfig (`components/rb_config`), generated reference `docs/configuration.md` (26) |
| 8 | Versioned communication protocols | ESP-NOW `RB_PROTOCOL_VERSION` (4), MQTT `schema_version` (21); both rejected when unsupported |
| 9 | Experimental thresholds configurable | every threshold and timeout in menuconfig, flagged UNVERIFIED |
| 10 | State machine independently testable | pure C with injected time; 47 Unity tests on host and target (28) |
| 11 | Log state transitions and faults clearly | transition callback → `SAFETY` log + MQTT event; fault manager logs every raise/clear (25) |
| 12 | One milestone at a time | bring-up projects test each sensor and board part on its own (02, 03, 06, 13, 14, 15, 17) before the full firmwares (05, 29); unit tests (28) check the logic before hardware |
