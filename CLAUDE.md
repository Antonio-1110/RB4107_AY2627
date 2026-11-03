# Notes for AI coding agents

Read [`README.md`](README.md), [`docs/overview.md`](docs/overview.md) and
[`docs/status.md`](docs/status.md) first. Decisions already made are in
[`docs/architecture.md`](docs/architecture.md#design-decisions): don't reopen
them without the user. Open work is in
[GitHub issues](https://github.com/Antonio-1110/RB4107_AY2627/issues); there
is no TODO file.

## How the user likes to work

- The user is learning ESP-IDF: explain options in plain terms, step by step.
- Don't change code without asking; explain the options first.
- Bench-test each part with its `firmware/testing/` project before the full system.
- The S3 must keep protecting the kitchen when MQTT, Django, the laptop or the
  network is down. Never add a dependency from the safety path on the
  telemetry path.
- Commit straight to `main` for now (a demo push): no PRs until the user says
  the demo is mature. Never push or merge without being asked.

## Safety rules: one table, code and doc

The controller's safety logic is a table:
[`firmware/components/safety/src/safety_rules.def`](firmware/components/safety/src/safety_rules.def).
The state machine in `safety.c` runs its transitions straight from that table,
and [`docs/safety_rules.md`](docs/safety_rules.md) and the dashboard's copy
(`frontend/js/safety_rules_data.js`) are generated from it. Don't
read the codebase to describe the rules, and never edit either generated file
by hand.

Every setting the rules text names as `{NAME}` can be changed from the
dashboard (Tuning → Safety): the step cards' fields cover some, and every other
one is a row of
[`firmware/components/rb_params/include/rb_params.def`](firmware/components/rb_params/include/rb_params.def)
(the "params" command, shown under the card its group names). A setting
written `{=NAME}` is fixed on purpose: menuconfig or `safety.c` only. Don't
make a fixed setting tunable again, or a tunable one fixed, without asking the
user. The same script generates `django/ingest/safety_params.json` and the
params keys of the MQTT schema from `rb_params.def`.

Every task, before you finish (and before opening or updating a PR):

1. Run `python3 tools/safety_rules.py check`. It must pass. It fails when:
   - the doc, the dashboard copy, `safety_params.json` or the schema's params
     keys are out of date (run `python3 tools/safety_rules.py gen`);
   - a setting the rules text names (`{NAME}`) can't be changed from the
     dashboard: add a row to `rb_params.def` (or a card field and its `LIVE`
     entry in `frontend/js/safety_logic.js`), or write `{=NAME}` if the user
     agreed it stays fixed;
   - the table names a guard, action or function that doesn't exist;
   - a function the table names (or `evaluate()`, `transition()`,
     `on_enter()`, `on_exit()`) changed on your branch but the table didn't.
     Then re-read the rules that name it, fix their text in the table, and
     regenerate.
2. To change when the controller does something, change the table: add,
   reorder or edit a `SAFETY_RULE` and its guard, not ad-hoc code in
   `evaluate()`. Signal rules (`SAFETY_SIGNAL`: fusion, cooking, timers,
   holds) are written next to the function that does the work; update their
   text when you change that function.
3. Run the safety tests after any change there: `tools/run_safety_tests.sh`
   (seconds, no idf.py build) or `tools/run_host_tests.sh` (everything).
   Add or update a test in `firmware/testing/unit_tests/main/test_safety.c`
   for any rule you add or change.
4. If the table and the code's behaviour disagree and you didn't cause it,
   tell the user. Never change behaviour just to match the doc without asking.
5. Say in the PR description that the check passed.
