// The Safety tab's Apply buttons (safety_save.js and the parts that register with it): each card sends only
// its own changes, a command shared by two cards keeps the other card's values as they are in force, a bad
// field stops the whole card, and the buttons and status follow what the controller answers.
// Run: node --test frontend/tests/

import assert from "node:assert/strict";
import { beforeEach, test } from "node:test";
import { installDom, installFetch, requests, settle } from "./fake_dom.mjs";

installDom();
installFetch();
const { $ } = await import("../js/dom.js");
const { refresh } = await import("../js/safety_save.js");
const { renderPresenceFilter } = await import("../js/presence_filter.js");
const { renderParams, PARAMS } = await import("../js/params.js");
const { renderAttendance } = await import("../js/attendance.js");
const { renderRadarRules } = await import("../js/radar_rules.js");
const { renderTiers } = await import("../js/tiers.js");
const { renderCookingTemp } = await import("../js/cooking_temp.js");
const { renderCookware } = await import("../js/cookware.js");

const defaults = Object.fromEntries(PARAMS.map((p) => [p.key, p.default]));

// What the controller reports, as the API hands it to the dashboard.
function telemetry(over = {}) {
  return {
    presence_filter: {
      absence_seconds: 1,
      return_seconds: 5,
      return_gap_seconds: 1,
      source: "menuconfig",
      fusion: "all",
      pause_budget_seconds: 30,
      pause_budget_pct: 25,
      attendance_source: "menuconfig",
    },
    warning_after_seconds: 150,
    shutdown_after_seconds: 300,
    shutdown_counts_from: "UNATTENDED",
    cooking_on_c: 50,
    cooking_off_c: 40,
    cooking_source: "menuconfig",
    cookware_profile: "mixed",
    cookware_source: "menuconfig",
    tiers: {
      levels: [
        { tier: 2, temp_c: 100, warning_seconds: 120, shutdown_seconds: 240 },
        { tier: 3, temp_c: 150, warning_seconds: 90, shutdown_seconds: 180 },
        { tier: 4, temp_c: 200, warning_seconds: 60, shutdown_seconds: 120 },
        { tier: 5, temp_c: 250, warning_seconds: 30, shutdown_seconds: 60 },
      ],
    },
    tiers_source: "menuconfig",
    radar_rules: {
      mode: "observe",
      quiet_gap_seconds: 1.5,
      dwell_seconds: 4,
      stationary_min_seconds: 1.5,
      settle_window_seconds: 4,
      settle_span_cm: 60,
      zone_max_cm: 0,
      source: "menuconfig",
    },
    params: { ...defaults },
    params_source: "menuconfig",
    ...over,
  };
}

let deviceN = 0,
  deviceId = "";
// What dashboard.js does on every poll.
function render(values = telemetry(), connection = "online") {
  const device = { device_id: deviceId, connection, values };
  for (const fn of [
    renderPresenceFilter,
    renderCookingTemp,
    renderCookware,
    renderTiers,
    renderParams,
    renderAttendance,
    renderRadarRules,
  ])
    fn(device);
  refresh();
}

beforeEach(() => {
  deviceId = `controller_${++deviceN}`; // a new device resets every part's state
  requests.length = 0;
  render();
});

const apply = async (card) => {
  $(`save-${card}`).click();
  await settle();
};
const sent = (path) => requests.filter((r) => r.path.endsWith(path)).map((r) => r.body);
const status = () => $("safety-status").textContent;

test("nothing changed: every Apply is off, Restore defaults is on", () => {
  for (const card of ["cooking", "person", "timer"]) assert.equal($(`save-${card}`).disabled, true, card);
  assert.equal($("safety-defaults").disabled, false);
});

test("a change turns on only its own card's Apply", () => {
  $("pf-absence").enter("3");
  refresh();
  assert.equal($("save-person").disabled, false);
  assert.equal($("save-timer").disabled, true);
  assert.equal($("save-cooking").disabled, true);
  assert.match(status(), /Not yet applied: absence and return confirmation/);
});

test("Person's Apply sends away/back as edited and the timer's times as in force", async () => {
  $("pf-absence").enter("3");
  $("pf-warning").enter("140"); // an unapplied Timer edit must not go out with Person
  refresh();
  await apply("person");
  assert.deepEqual(sent("/presence_filter/"), [
    { return_gap_ms: 1000, absence_ms: 3000, return_ms: 5000, warning_ms: 150000, shutdown_ms: 300000 },
  ]);
  assert.equal(requests.length, 1, "only the presence command");
  assert.match(status(), /Absence and return confirmation: sent/);
  assert.equal($("pf-warning").value, "140", "the Timer edit is still in its field");
});

test("Timer's Apply sends the times as edited and away/back as in force", async () => {
  $("pf-absence").enter("3");
  $("pf-warning").enter("140");
  refresh();
  await apply("timer");
  const [presence] = sent("/presence_filter/");
  assert.equal(presence.absence_ms, 1000);
  assert.equal(presence.warning_ms, 140000);
  // The tiers didn't change, so Timer's Apply sends no tiers command.
  assert.deepEqual(sent("/tiers/"), []);
});

test("Person's Apply covers every Person field: attendance and radar rules too", async () => {
  $("pf-return").enter("6");
  $("at-budget").enter("45");
  $("rr-mode").enter("enforce");
  refresh();
  await apply("person");
  assert.equal(sent("/presence_filter/")[0].return_ms, 6000);
  assert.deepEqual(sent("/attendance/"), [{ pause_budget_s: 45, pause_budget_pct: 25, fusion: "all" }]);
  assert.equal(sent("/radar-rules/")[0].mode, "enforce");
});

test("the More settings of one card keep the other card's values as in force", async () => {
  $("pr-falling_hold_max_ms").enter("80");
  $("pr-fault_shutdown_ms").enter("100"); // Timer's More settings, not applied
  refresh();
  await apply("cooking");
  assert.deepEqual(sent("/params/"), [{ ...defaults, falling_hold_max_ms: 80000 }]);
});

test("each params setting is in the More settings of the card its group names", () => {
  for (const card of ["cooking", "timer"]) {
    const inCard = PARAMS.filter((p) => p.group.toLowerCase() === card).length;
    assert.ok(inCard > 0, `rb_params.def has a ${card} group`);
    assert.equal($(`pr-fields-${card}`).children.length, inCard, card);
  }
});

test("one bad field stops the whole card, and says which", async () => {
  $("pf-absence").enter("3");
  $("at-budget").enter("999");
  refresh();
  await apply("person");
  assert.deepEqual(requests, []);
  assert.match(status(), /countdown pause for passers-by must be 0–600 s/);
});

test("a shut-off not after the buzzer is refused before sending", async () => {
  $("pf-shutdown").enter("100"); // the buzzer is at 150 s
  refresh();
  await apply("timer");
  assert.deepEqual(requests, []);
  assert.match(status(), /gas shut-off time must be longer than the buzzer warning time/);
});

test("tiers must not start below the tier before them", async () => {
  $("tr-3-temp").enter("90"); // tier 2 starts at 100 °C
  refresh();
  await apply("timer");
  assert.deepEqual(requests, []);
  assert.match(status(), /tier 3 must not start below/);
});

test("waiting for the controller: the card is off, then 'saved' once telemetry matches", async () => {
  $("ct-value").enter("62.5");
  refresh();
  await apply("cooking");
  assert.deepEqual(sent("/cooking_temp/"), [{ cooking_temp_c: 62.5 }]);
  render();
  assert.equal($("save-cooking").disabled, true, "off while waiting");
  assert.equal($("ct-value").disabled, true);
  render(telemetry({ cooking_on_c: 62.5, cooking_source: "dashboard" }));
  assert.match(status(), /Cooking temperature: saved, 62.5 °C/);
  assert.doesNotMatch(status(), /Not applied yet/);
  assert.equal($("ct-value").disabled, false);
});

test("Restore defaults sends each command once, the shared ones too", async () => {
  $("safety-defaults").click();
  await settle();
  const paths = requests.map((r) => r.path.split("/").at(-2)).sort();
  assert.deepEqual(paths, ["attendance", "cooking_temp", "cookware", "params", "presence_filter", "radar-rules", "tiers"]);
  assert.ok(requests.every((r) => r.body.defaults === true));
});

test("an offline controller: nothing can be changed, and the page says why", () => {
  render(telemetry(), "offline");
  assert.equal($("safety-form").disabled, true);
  assert.equal($("safety-defaults").disabled, true);
  assert.match(status(), /controller is offline/);
});
