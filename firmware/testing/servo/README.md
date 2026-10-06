# Test: servo (valve node)

> **Test only, not part of the running system.** Flash
> [`valve_node`](../../valve_node) on this board afterwards.

Target: the valve node's ESP32, whichever chip it is. Moves the servo
between the CLOSED and OPEN angles every 3 s. It uses the same menuconfig
settings as `valve_node` (**RB4107 configuration → Valve node**), so the
angles you settle on here carry over.

```bash
idf.py set-target esp32c6        # your chip: esp32, esp32s3, esp32c3, esp32c6
idf.py menuconfig                # optional: servo GPIO, angles, pulse widths
idf.py -p <PORT> flash monitor
```

Wiring and default pins: [`valve_node/README.md`](../../valve_node/README.md#wiring).

Check:
1. The log alternates `CLOSED: 0 deg` and `OPEN: 90 deg`.
2. The valve handle turns the right way. If it doesn't, swap the two angles.
3. The servo doesn't buzz or strain at either end. If it does, adjust the
   angles or narrow the pulse range (e.g. 1000-2000 us).
