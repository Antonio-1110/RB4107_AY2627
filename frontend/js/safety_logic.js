"use strict";
// Safety logic card: the controller's rules, from the same table its state machine
// runs (firmware/components/safety/src/safety_rules.def, copied into
// safety_rules_data.js by tools/safety_rules.py gen). It shows the state the
// controller reports, the rules that can fire next, any hold, and which rule
// fired last (matched from the event log by its reported reason). It only reads.

import RULES from "./safety_rules_data.js";
import { $, make } from "./dom.js";
import { when } from "./format.js";

const HOLD_RULE = { cooling: "D2", return: "D3" };
const HOLD_TEXT = {
  cooling: "The next step is held: the pan is only cooling down.",
  return: "The next step is held while a return is being confirmed.",
};

let device = null,
  events = [];

// "**bold**" marks the menuconfig defaults in the generated text.
function rich(text) {
  const out = document.createDocumentFragment();
  text.split("**").forEach((part, i) => {
    if (part) out.append(i % 2 ? make("strong", part) : document.createTextNode(part));
  });
  return out;
}

const stateOf = (name) => RULES.states.find((s) => s.state === name);
const outputsText = (s) => `buzzer ${s.buzzer} · shutdown ${s.shutdown ? "ACTIVE" : "released"}`;

// The newest state change in the event log, and the rule that made it.
function lastChange() {
  for (const event of events) {
    const safety = event.detail && event.detail.safety;
    if (!safety || !safety.reason || !safety.from_state) continue;
    const rule = RULES.transitions.find(
      (r) => r.reason === safety.reason && r.from.includes(safety.from_state) && r.to === safety.state,
    );
    return { rule, safety, at: event.received_at };
  }
  return null;
}

function ruleRow(rule, { current, fired }) {
  const row = make("li", null, `logic-rule${fired ? " fired" : ""}${current ? " current" : ""}`);
  row.append(make("span", rule.id, "logic-id"));
  const body = make("span", null, "logic-when");
  body.append(rich(rule.when));
  row.append(body, make("span", `→ ${rule.to}`, `logic-to to-${rule.to.toLowerCase()}`));
  if (fired) row.title = "The rule that made the last state change";
  return row;
}

function renderNow(state, last) {
  const host = $("logic-now");
  host.replaceChildren();
  const s = stateOf(state);
  if (!s) {
    host.append(make("p", "Waiting for the controller's state.", "muted"));
    return;
  }
  const line = make("p", null, "logic-now-line");
  line.append(make("span", state, `state-badge logic-state st-${state.toLowerCase()}`), ` ${outputsText(s)}`);
  host.append(line);
  const hold = device && device.values ? device.values.hold : null;
  if (HOLD_TEXT[hold]) host.append(make("p", `${HOLD_TEXT[hold]} (${HOLD_RULE[hold]})`, "logic-hold"));
  if (last)
    host.append(
      make(
        "p",
        `Last change: ${last.rule ? last.rule.id : "?"} ${last.safety.from_state} → ${last.safety.state}` +
          ` (${last.safety.reason}), ${when(last.at)}`,
        "muted small",
      ),
    );
}

function renderNext(state) {
  const list = $("logic-next");
  list.replaceChildren();
  const next = RULES.transitions.filter((r) => r.from.includes(state));
  if (!next.length) {
    list.append(make("li", "No state reported yet.", "muted"));
    return;
  }
  for (const rule of next) list.append(ruleRow(rule, { current: true, fired: false }));
}

function renderAll(state, last) {
  const table = $("logic-transitions");
  table.replaceChildren();
  let group = null;
  for (const rule of RULES.transitions) {
    const key = rule.from.join(", ");
    if (key !== group) {
      group = key;
      table.append(
        make("li", rule.from.length > 3 ? "Any cooking-related state" : key, "logic-group"),
      );
    }
    table.append(
      ruleRow(rule, { current: rule.from.includes(state), fired: last && last.rule === rule }),
    );
  }

  const outputs = $("logic-outputs");
  outputs.replaceChildren();
  for (const s of RULES.states) {
    const row = make("tr", null, s.state === state ? "current" : "");
    row.append(make("td", s.state), make("td", s.buzzer), make("td", s.shutdown ? "ACTIVE" : "released"));
    outputs.append(row);
  }

  const signals = $("logic-signals");
  signals.replaceChildren();
  const hold = device && device.values ? HOLD_RULE[device.values.hold] : null;
  for (const section of RULES.sections) {
    if (!section.rows.length) continue;
    const box = make("details", null, "logic-section");
    box.open = section.key === "D" && Boolean(hold);
    box.append(make("summary", section.title));
    const list = make("ul", null, "logic-list");
    for (const row of section.rows) {
      const item = make("li", null, `logic-signal${row.id === hold ? " current" : ""}`);
      const text = make("span", null, "logic-when");
      text.append(rich(row.when), " → ", rich(row.result));
      item.append(make("span", row.id, "logic-id"), text, make("code", row.code, "logic-code"));
      list.append(item);
    }
    box.append(list);
    signals.append(box);
  }
}

function render() {
  const state = device && device.values && device.values.safety_state ? device.values.safety_state.toUpperCase() : "";
  const last = lastChange();
  renderNow(state, last);
  renderNext(state);
  renderAll(state, last);
}

export function renderSafetyLogic(next) {
  if (next && device && next.device_id !== device.device_id) events = [];
  device = next;
  render();
}

export function setSafetyEvents(list) {
  events = Array.isArray(list) ? list : [];
  render();
}
