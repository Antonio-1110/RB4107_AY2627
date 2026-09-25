# 13 – Buzzer driver

TODO section 13. Target: **Waveshare ESP32-S3-ETH-8DI-8RO**.

## Interface (UNCONFIRMED)

The legacy controller drove the buzzer on **GPIO46, active high**, and that
is the default. Check the Waveshare schematic to confirm it, and to find out
whether the buzzer is **active** (DC level) or **passive** (needs a tone).
Both are supported: menuconfig → *Controller board* → *Buzzer* → *Buzzer type*.

## API (`components/buzzer`)

```c
buzzer_init(&cfg);                        // pin configured, silent
buzzer_on();                              // continuous
buzzer_off();
buzzer_set_pattern(BUZZER_PATTERN_WARNING);   // non-blocking, played by an esp_timer
```

| Safety state | Pattern |
|---|---|
| WARNING | 200 ms on / 800 ms off |
| SHUTDOWN | 150 ms on / 150 ms off |
| FAULT | double chirp every 2 s |

This demo plays each pattern for 4 s in a loop.
