# 14 – Relay / shutdown driver

> **Test-only project, not part of the running system.** It checks one part on its own and replaces the system firmware on that board. To run the
> system, flash [`05a_c6_presence_node`](../05a_c6_presence_node) on the two
> presence C6 boards, [`05b_c6_thermal_node`](../05b_c6_thermal_node) on the
> thermal C6 board and [`29_end_to_end`](../29_end_to_end) on the S3.

TODO section 14. Target: **Waveshare ESP32-S3-ETH-8DI-8RO**.

## Interface

The 8 relays are driven by a **TCA9554** I2C expander (address `0x20` on the
board I2C bus, SDA 42 / SCL 41). This comes from the legacy controller and is
UNCONFIRMED.

## API (`components/shutdown_output`)

```c
shutdown_output_init(&cfg);   // all relays to the safe boot state
shutdown_activate();          // cut power to the appliance
shutdown_release();           // restore power
shutdown_verify();            // read the expander back
```

The state machine never touches GPIO or I2C. `components/rb_outputs` maps
its outputs onto this API and the buzzer.

## Configuration (menuconfig → *Controller board* → *Shutdown relay*)

| Option | Default | Note |
|---|---|---|
| `RB_SHUTDOWN_RELAY_CHANNEL` | 1 | |
| `RB_RELAY_ACTIVE_LEVEL` | 1 | UNCONFIRMED |
| `RB_SHUTDOWN_POLARITY` | energise to shut down | OPEN QUESTION: depends on NO/NC wiring. *De-energise to shut down* is fail-safe: the appliance loses power if the controller loses power. |
| `RB_SHUTDOWN_BOOT_STATE` | released | OPEN QUESTION |

## Boot glitch protection

After power-on the TCA9554 pins are inputs. The driver writes the **output
register first** and only then switches the pins to outputs, so a relay can't
click during boot. (The legacy driver did it in the opposite order.) If a
read-back doesn't match what was written (e.g. the expander reset after a
brownout), the registers are restored and a fault is reported.
