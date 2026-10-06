# Test: wireless valve link (S3)

> **Test only, not part of the running system.** Flash
> [`controller`](../../controller) on the S3 afterwards.

Target: **ESP32-S3** (the controller board). It runs the controller's
wireless valve sender (`components/rb_valve_link`) without sensors or the
safety state machine, and cycles through:

| Phase | Length | S3 sends | Valve node should |
|---|---|---|---|
| SAFE | 20 s | KEEP_OPEN every 1 s | open |
| SHUTDOWN | 10 s | CLOSE | close at once (reason: controller sent CLOSE) |
| SAFE | 20 s | KEEP_OPEN | reopen |
| SILENT | 10 s | CLOSE (the "safety task" stopped reporting) | close |

Setup:
1. Flash the wireless [`valve_node`](../../valve_node#step-2b-wireless-version)
   and note the MAC it prints.
2. Here:
   ```bash
   idf.py menuconfig    # Controller board → Gas valve node → Valve node MAC (broadcast also works)
   idf.py -p <PORT> flash monitor
   ```
3. The S3 logs `valve node_04 reports OPEN/CLOSED` as the node answers.

To check the valve node's own timeout, unplug the S3 while the valve is
open. The node closes about 3 s later.
