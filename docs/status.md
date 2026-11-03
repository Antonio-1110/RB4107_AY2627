# Status: what is proven and what is not

Last reviewed 2026-11-03. This page records what the repo itself shows. Where
the bench result isn't written down anywhere in the repo, it says so: update
the table when you bench-test something. The detailed gap to a real
deployment is in [demo_vs_deployment.md](demo_vs_deployment.md); open work is in
[GitHub issues](https://github.com/Antonio-1110/RB4107_AY2627/issues).

**Levels:** *Emulated* = host unit tests, the controller in QEMU with simulated
nodes, or Django against a real broker. *Bench* = run on the real boards.

| Part | Emulated | Bench | Open |
|---|---|---|---|
| Safety state machine and timers | yes: safety unit tests, QEMU | not recorded | timer values are a baseline, not tuned on real cooks |
| Shutdown output (output 1) | yes | active level confirmed on the bench (low pin = output on) | boot state is an **open question**; no hardware watchdog if the safety task hangs |
| Buzzer, RTC, board bring-up | n/a | bench-test projects exist in `firmware/testing/` | some bench-found pins are not yet the Kconfig defaults (issue #19) |
| C4002 radars | protocol and tuning logic tested | bench-test project exists | detection thresholds, angle and enclosure not measured; radar rules are placeholders, shown but not enforced by default |
| MLX90640 thermal camera | feature extraction tested | bench-test project exists | cooking temperature (50 °C on, 40 °C off) is a placeholder (issue #21); shiny pans read cold; one trend for the whole frame |
| Cooking judgment (is heat still going in?) | yes, simulated cooling curves | needs several minutes on real hardware | cookware time constants are estimates |
| Presence fusion and passer-by pause | yes | not recorded | needs recorded real walk-bys; a false target on either radar keeps the stove on (issue #20) |
| ESP-NOW link and protocol v3 | yes | in use on the bench | unencrypted, CRC only |
| MQTT, Django, dashboard | yes, with a real broker | in use with the bench controller | no login, no TLS; the dashboard can reset a shutdown remotely |
| Retention and stall history | yes; demo data for several stalls | n/a | retention numbers to be confirmed with the data owner |
| Telegram alerts | built | not recorded | depends on the laptop and internet; SMS would need a registered sender ID |
| Full build (`idf.py`) | n/a | n/a | passed on `main` when last recorded; not re-run since the valve node was removed |

## What a reader can safely say today

- The complete chain (sensors → controller → buzzer and shutdown output →
  dashboard) works in emulation, and parts of it run on the bench.
- The timing, thresholds and radar rules are a **baseline**. They show the
  design, not a tuned or certified safety product.
- It has **not** been tested on a real stove or with a real appliance
  supply, and has no safety certification.
