# Test: relay / shutdown driver

> **Test only, not part of the running system.** It checks the shutdown relay on its own. Flashing it
> replaces the system firmware on that board. To run the system, flash
> [`presence_node`](../../presence_node) on the two presence C6 boards,
> [`thermal_node`](../../thermal_node) on the thermal C6 board and
> [`controller`](../../controller) on the S3.

Target: **Waveshare ESP32-S3-POE-ETH-8DI-8DO**.

## Interface

This board has **no relays**. Its 8 digital outputs are opto-isolated
Darlington transistors that switch to GND (up to 500 mA each, flyback diodes
fitted). Terminal block: COM (to the + of the load supply), GND, outputs 1–8.
To cut the appliance's power, wire the coil of an external relay or contactor
(DC coil, under 500 mA) between COM and one output.

The outputs are driven by a **TCA9554** I2C expander at `0x20` on the board
I2C bus (SDA 42 / SCL 41), confirmed on the bench. The code and menuconfig
still call it the "shutdown relay": that means the external relay.

## API (`components/shutdown_output`)

```c
shutdown_output_init(&cfg);   // all outputs to the safe boot state
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
| `RB_SHUTDOWN_POLARITY` | energise to shut down | OPEN QUESTION: depends on whether the appliance is on the external relay's NO or NC contact. *De-energise to shut down* is fail-safe: the appliance loses power if the controller loses power. |
| `RB_SHUTDOWN_BOOT_STATE` | released | OPEN QUESTION |

## Wired valve line

With **Gas valve node → Wired valve line: output channel** set (2 for the
demo), that output toggles with the shutdown relay: ON while released, OFF
while shut down. A wired [`valve_node`](../../valve_node) on it should open
and close the valve every 5 s.

## Boot glitch protection

After power-on the TCA9554 pins are inputs. The driver writes the **output
register first** and only then switches the pins to outputs, so an output
can't switch on during boot. (The earlier prototype driver did it in the opposite order.) If a
read-back doesn't match what was written (e.g. the expander reset after a
brownout), the registers are restored and a fault is reported.
