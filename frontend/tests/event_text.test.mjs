// The Event log says what happened ("Person left the stove"), never just "State change" or "Warning".
// Run: node --test frontend/tests/

import assert from "node:assert/strict";
import { test } from "node:test";

import RULES from "../js/safety_rules_data.js";
import { REASONS, eventText } from "../js/event_text.js";

const event = (event_type, reason, extra = {}) => ({
  event_type,
  detail: { safety: { reason, unattended_ms: 0, ...extra.safety }, fault: extra.fault ?? null },
});

test("every reason in the rules table has its own wording", () => {
  for (const { reason } of RULES.transitions) {
    assert.ok(reason in REASONS, `add "${reason}" to REASONS in event_text.js`);
    assert.doesNotMatch(eventText(event("state_change", reason)).title, /state change/i);
  }
});

test("buzzer and shut-off say what happened and how long the person was away", () => {
  const buzzer = eventText(event("warning", "unattended timeout", { safety: { unattended_ms: 150000 } }));
  assert.equal(buzzer.title, "Buzzer warning: nobody returned in time");
  assert.equal(buzzer.note, "After 2 min 30 s unattended");
  assert.equal(eventText(event("shutdown", "shutdown timeout")).title, "Gas shut off");
});

test("faults name the problem, and when it is fixed", () => {
  const fault = { name: "presence_b_node_offline", class: "SAFETY" };
  assert.equal(eventText(event("fault_raised", null, { fault })).title, "Presence node B offline");
  assert.equal(eventText(event("fault_cleared", null, { fault })).title, "Presence node B offline: cleared");
  // A fault the dashboard doesn't know yet still reads as words.
  assert.equal(eventText(event("fault_raised", null, { fault: { name: "new_sensor_gone" } })).title, "New sensor gone");
});

test("a restart, and a reason the dashboard doesn't know yet", () => {
  assert.equal(eventText({ event_type: "controller_restarted", detail: { note: "New boot" } }).title, "Controller restarted");
  assert.equal(eventText(event("state_change", "something new")).title, "Something new");
});
