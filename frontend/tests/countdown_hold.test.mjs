// The Countdown card names the kind of hold that is holding back a due buzzer warning or shut-off:
// cooling (sure, no heat input), falling (not sure yet) or return (a person is being confirmed).
// Run: node --test frontend/tests/

import assert from "node:assert/strict";
import { test } from "node:test";
import { installDom, installFetch } from "./fake_dom.mjs";

// The card redraws on a timer; don't let it keep the test process alive.
const realSetInterval = globalThis.setInterval;
globalThis.setInterval = (fn, ms) => {
  const handle = realSetInterval(fn, ms);
  handle.unref();
  return handle;
};
installDom();
installFetch();
const { $ } = await import("../js/dom.js");
const { renderCountdown } = await import("../js/countdown.js");
// The fake page has no clock wrapper to hide and no styled bar or marks.
$("cd-seconds").closest = () => ({});
$("cd-fill").style = {};
$("cd-mark-warn").style = {};
$("cd-mark-stop").style = {};

function show(values) {
  renderCountdown({
    connection: "online",
    field_age_seconds: {},
    values: {
      safety_state: "unattended",
      unattended_seconds: 100,
      warning_after_seconds: 150,
      shutdown_after_seconds: 300,
      shutdown_counts_from: "UNATTENDED",
      cooking_active: true,
      params: { falling_hold_max_ms: 90000, cooling_hold_max_ms: 3600000 },
      ...values,
    },
  });
  return { label: $("cd-clock-label").textContent, title: $("cd-phase").textContent, detail: $("cd-detail").textContent };
}

test("a cooling hold says the controller is sure no heat is going in, and its limit", () => {
  const card = show({ hold: "cooling", cookware_tau_s: 600, cooking_room_c: 36 });
  assert.match(card.label, /^hold: cooling/);
  assert.match(card.title, /pan cooling down/);
  assert.doesNotMatch(card.label + card.title, /\(/);
  assert.match(card.detail, /Hold type: COOLING/);
  assert.match(card.detail, /Limit: 40 min/); // 4 x 10 min
});

test("a falling hold says the controller is not sure yet, and its limit", () => {
  const card = show({ hold: "falling" });
  assert.match(card.label, /^hold: falling$/);
  assert.doesNotMatch(card.label + card.title, /\(/);
  assert.match(card.detail, /Hold type: FALLING/);
  assert.match(card.detail, /cannot yet tell/);
  assert.match(card.detail, /Limit: 90 s/);
});

test("a return hold says a person is being confirmed", () => {
  const card = show({ hold: "return" });
  assert.match(card.label, /^hold: return/);
  assert.match(card.detail, /Hold type: RETURN/);
});

test("no hold shows no hold wording", () => {
  const card = show({ hold: "none" });
  assert.doesNotMatch(card.label, /hold/);
});

test("checking: no countdown yet, the time away already counts, and its limit", () => {
  const card = show({ safety_state: "monitoring", hold: "checking", absent_seconds: 12, unattended_seconds: 0 });
  assert.equal(card.title, "Checking: stove off or turned down?");
  assert.match(card.label, /^away 12 s, already counted$/);
  assert.match(card.detail, /Limit: 90 s/);
  assert.equal($("cd-seconds").textContent, "check");
  assert.equal($("cd-timer-live").textContent, "Not started yet");
});
