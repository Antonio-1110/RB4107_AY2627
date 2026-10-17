# Notes for AI coding agents

Read [`docs/agent-context/handover.md`](docs/agent-context/handover.md) first.

## Every task: check that the safety rules doc matches the code

[`docs/safety_rules.md`](docs/safety_rules.md) is the list of every condition
the controller reacts to and what it does. It must always describe what the
code on your branch does. Before you finish any task (and before opening or
updating a PR):

1. Run `python3 tools/check_safety_rules_doc.py`. It checks the settings and
   constants tables against the Kconfig and `safety.c`. It must pass.
2. If your change touches any of these, re-read the matching rules and confirm
   each one against the code. The script can't see behaviour, only numbers:
   - `firmware/components/safety/` (state machine, trend, tiers, holds)
   - `firmware/components/sensor_node/` (radar fusion, radar rules, node health)
   - `firmware/components/rb_safety_config/`, `rb_controller/`, `rb_outputs/`,
     `rb_valve_link/`, `shutdown_output/`
   - the `RB_SAFETY_*`, `RB_CTRL_PRESENCE_FUSION`, `RB_CTRL_RADAR_*`,
     `RB_CTRL_NODE_*` or `RB_SHUTDOWN_*` options in
     `firmware/components/rb_config/Kconfig`
   - what the dashboard can change about any of the above
3. Update `docs/safety_rules.md` (and `docs/safety_state_machine.md` where it
   explains the same rule) in the same PR. If you find a mismatch you did not
   cause, fix the doc to match the code and tell the user. Never change the
   code to fit the doc without asking.
4. Say in your PR description that the check passed, or what you changed in
   the doc.
