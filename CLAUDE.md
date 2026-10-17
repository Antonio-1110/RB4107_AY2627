# Notes for AI coding agents

Read [`docs/agent-context/handover.md`](docs/agent-context/handover.md) first.

## Safety rules: one table, code and doc

The controller's safety logic is a table:
[`firmware/components/safety/src/safety_rules.def`](firmware/components/safety/src/safety_rules.def).
The state machine in `safety.c` runs its transitions straight from that table,
and [`docs/safety_rules.md`](docs/safety_rules.md) and the dashboard's copy
(`frontend/js/safety_rules_data.js`) are generated from it. Don't
read the codebase to describe the rules, and never edit either generated file
by hand.

Every number and choice in the rules can be changed from the dashboard: the
Rules tab's cards cover some, and every other one is a row of
[`firmware/components/rb_params/include/rb_params.def`](firmware/components/rb_params/include/rb_params.def)
(the "params" command, the Logic tab's Other settings card). The same script
generates `django/ingest/safety_params.json` and the params keys of the MQTT
schema from it.

Every task, before you finish (and before opening or updating a PR):

1. Run `python3 tools/safety_rules.py check`. It must pass. It fails when:
   - the doc, the dashboard copy, `safety_params.json` or the schema's params
     keys are out of date (run `python3 tools/safety_rules.py gen`);
   - a setting the rules text names (`{NAME}`) can't be changed from the
     dashboard: add a row to `rb_params.def` (or a card and its `LIVE` entry
     in `frontend/js/safety_logic.js`);
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
