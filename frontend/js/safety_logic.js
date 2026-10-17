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

// The generated text marks each setting as {{NAME|menuconfig default}}. LIVE maps the
// ones the controller reports to its telemetry value; tune: true marks the ones the
// dashboard's Tuning cards can change. Anything else shows its menuconfig default.
const num = (v) => typeof v === "number" && Number.isFinite(v);
const trim = (v) => String(Math.round(v * 10) / 10);
const secs = (v) => (num(v) ? `${trim(v)} s` : null);
const degC = (v) => (num(v) ? `${trim(v)} °C` : null);
const rate = (v) => (num(v) ? `${trim(v)} °C/min` : null);
const from = (obj, ...keys) => keys.reduce((o, k) => (o && typeof o === "object" ? o[k] : undefined), obj);

const FUSION = { all: "every working radar sees them", any: "either radar sees them" };
const RADAR_MODE = {
  off: "off",
  observe: "observe: computed and reported, not used",
  enforce: "enforce: a radar that only sees someone passing does not count as at the stove",
};
const COOKWARE = { mixed: "mixed / unknown", light: "light pans", heavy: "heavy pans", liquid: "pots with liquid" };
const COUNTS_FROM = { UNATTENDED: "start of UNATTENDED (total unattended time)", WARNING: "start of WARNING" };

const pf = (key) => (v) => secs(from(v, "presence_filter", key));
const tier = (t, key, fmt) => (v) => fmt(from(v, "tiers", t, key));
const radar = (key) => (v) => secs(from(v, "radar_rules", key));
const LIVE = {
  RB_SAFETY_HEAT_ON_DC: { tune: true, get: (v) => degC(v.cooking_on_c) },
  RB_SAFETY_HEAT_OFF_DC: { tune: true, get: (v) => degC(v.cooking_off_c) },
  RB_SAFETY_ABSENCE_DEBOUNCE_MS: { tune: true, get: pf("absence_seconds") },
  RB_SAFETY_PRESENCE_RETURN_DEBOUNCE_MS: { tune: true, get: pf("return_seconds") },
  RB_SAFETY_PRESENCE_RETURN_GAP_MS: { tune: true, get: pf("return_gap_seconds") },
  RB_SAFETY_PAUSE_BUDGET_S: { tune: true, get: pf("pause_budget_seconds") },
  RB_SAFETY_PAUSE_BUDGET_PCT: {
    tune: true,
    get: (v) => (num(from(v, "presence_filter", "pause_budget_pct")) ? `${v.presence_filter.pause_budget_pct} %` : null),
  },
  RB_CTRL_PRESENCE_FUSION: { tune: true, get: (v) => FUSION[from(v, "presence_filter", "fusion")] || null },
  RB_SAFETY_WARNING_TIMEOUT_S: { tune: true, get: (v) => secs(v.warning_after_seconds) },
  RB_SAFETY_SHUTDOWN_TIMEOUT_S: { tune: true, get: (v) => secs(v.shutdown_after_seconds) },
  RB_SAFETY_SHUTDOWN_TIMING: { tune: false, get: (v) => COUNTS_FROM[v.shutdown_counts_from] || null },
  RB_SAFETY_COOKWARE: {
    tune: true,
    get: (v) =>
      COOKWARE[v.cookware_profile]
        ? COOKWARE[v.cookware_profile] + (num(v.cookware_tau_s) ? ` (${trim(v.cookware_tau_s / 60)} min)` : "")
        : null,
  },
  RB_SAFETY_TIER1_TEMP_DC: { tune: true, get: tier("elevated", "temp_c", degC) },
  RB_SAFETY_TIER1_WARNING_S: { tune: true, get: tier("elevated", "warning_seconds", secs) },
  RB_SAFETY_TIER1_SHUTDOWN_S: { tune: true, get: tier("elevated", "shutdown_seconds", secs) },
  RB_SAFETY_TIER2_TEMP_DC: { tune: true, get: tier("critical", "temp_c", degC) },
  RB_SAFETY_TIER2_RATE_DC_PER_MIN: { tune: true, get: tier("critical", "rate_c_per_min", rate) },
  RB_SAFETY_TIER2_RATE_MIN_TEMP_DC: { tune: true, get: tier("critical", "rate_min_temp_c", degC) },
  RB_SAFETY_TIER2_WARNING_S: { tune: true, get: tier("critical", "warning_seconds", secs) },
  RB_SAFETY_TIER2_SHUTDOWN_S: { tune: true, get: tier("critical", "shutdown_seconds", secs) },
  RB_CTRL_RADAR_RULES: { tune: true, get: (v) => RADAR_MODE[from(v, "radar_rules", "mode")] || null },
  RB_CTRL_RADAR_DWELL_MS: { tune: true, get: radar("dwell_seconds") },
  RB_CTRL_RADAR_STATIONARY_MIN_MS: { tune: true, get: radar("stationary_min_seconds") },
  RB_CTRL_RADAR_SETTLE_WINDOW_MS: { tune: true, get: radar("settle_window_seconds") },
  RB_CTRL_RADAR_SETTLE_SPAN_CM: {
    tune: true,
    get: (v) => (num(from(v, "radar_rules", "settle_span_cm")) ? `${v.radar_rules.settle_span_cm} cm` : null),
  },
};

function value(name, fallback) {
  const unquoted = fallback.replace(/^"(.*)"$/, "$1");
  const spec = LIVE[name];
  const live = spec && device && device.values ? spec.get(device.values) : null;
  const node = make("strong", live || unquoted, `logic-val${spec && spec.tune ? " tunable" : ""}${live ? "" : " default"}`);
  node.title = live
    ? `Reported by the controller${spec.tune ? ", tunable on the dashboard" : ""} (menuconfig default ${unquoted})`
    : spec && spec.tune
      ? "Menuconfig default; tunable on the dashboard (not reported yet)"
      : `Menuconfig default (${name}); change it in menuconfig or the code`;
  return node;
}

function rich(text) {
  const out = document.createDocumentFragment();
  let at = 0;
  for (const m of text.matchAll(/\{\{(\w+)\|([^}]*)\}\}/g)) {
    if (m.index > at) out.append(document.createTextNode(text.slice(at, m.index)));
    out.append(value(m[1], m[2]));
    at = m.index + m[0].length;
  }
  if (at < text.length) out.append(document.createTextNode(text.slice(at)));
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
