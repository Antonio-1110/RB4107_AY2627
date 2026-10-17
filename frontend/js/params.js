"use strict";
// Other settings (Tuning > Logic): every number and choice in the safety rules that has no card of its own,
// from the controller's table firmware/components/rb_params/include/rb_params.def (copied into
// safety_rules_data.js by tools/safety_rules.py gen). The controller checks them, saves them and answers
// with params / params_source in its telemetry. Values travel in the table's units (ms, 0.1 °C, ...).

import RULES from "./safety_rules_data.js";
import { sendParams } from "./api.js";
import { $, make } from "./dom.js";

const ANSWER_WAIT_MS = 15000;
// Wire unit -> [shown = wire / scale, unit, input step]
const KIND = {
  MS: [1000, "s", 0.1],
  DC: [10, "°C", 0.1],
  DC_PER_MIN: [10, "°C/min", 0.1],
  TENTHS: [10, "", 0.1],
  COUNT: [1, "", 1],
};

export const PARAMS = RULES.params;

// A value in the table's units as the dashboard writes it ("90 s", "start of WARNING").
export function paramText(p, v) {
  if (p.options) return p.options[v] ?? String(v);
  const [scale, unit] = KIND[p.kind];
  return `${Math.round((v / scale) * 10) / 10}${unit ? ` ${unit}` : ""}`;
}

let deviceId = "",
  edited = false,
  sending = false,
  pending = null; // {request_id, sentAt, expect: {key: value} or "menuconfig"}

const inputId = (p) => `pr-${p.key}`;

// One labelled input or select per setting, grouped as in the table.
function build() {
  const host = $("pr-fields");
  let grid = null,
    group;
  for (const p of PARAMS) {
    if (p.group !== group || !grid) {
      group = p.group;
      host.append(make("p", (group || "Other").toUpperCase(), "eyebrow"));
      grid = make("div", null, "tuning-grid params-grid");
      host.append(grid);
    }
    const label = make("label", p.options || !KIND[p.kind][1] ? p.label : `${p.label} (${KIND[p.kind][1]})`);
    let input;
    if (p.options) {
      input = make("select");
      p.options.forEach((text, i) => {
        const option = make("option", text);
        option.value = String(i);
        input.append(option);
      });
    } else {
      const [scale, , step] = KIND[p.kind];
      input = make("input");
      input.type = "number";
      input.min = String(p.min / scale);
      input.max = String(p.max / scale);
      input.step = String(step);
    }
    input.id = inputId(p);
    input.disabled = true;
    input.title = `Menuconfig default: ${paramText(p, p.default)} · key ${p.key}`;
    input.addEventListener("input", () => {
      edited = true;
      setStatus("Not applied yet.", "warn");
    });
    label.append(input);
    grid.append(label);
  }
}

function setStatus(text, style = "") {
  const status = $("pr-status");
  status.textContent = text;
  status.className = `tuning-status ${style || "muted"}`;
}

function show(p, v) {
  const input = $(inputId(p));
  input.value = p.options ? String(v) : String(Math.round((v / KIND[p.kind][0]) * 1000) / 1000);
}

// The value an input holds, in the table's units, or null if it is out of range.
function read(p) {
  const input = $(inputId(p));
  if (input.value === "") return null;
  const v = p.options ? Number(input.value) : Math.round(Number(input.value) * KIND[p.kind][0]);
  return Number.isInteger(v) && v >= p.min && v <= p.max ? v : null;
}

const changed = (reported) => PARAMS.filter((p) => reported[p.key] !== p.default).length;

function answered(values) {
  if (pending.expect === "menuconfig") return values.params_source === "menuconfig";
  return (
    values.params_source === "dashboard" &&
    values.params &&
    PARAMS.every((p) => values.params[p.key] === pending.expect[p.key])
  );
}

export function renderParams(device) {
  if (!device) return;
  if (device.device_id !== deviceId) {
    deviceId = device.device_id;
    edited = false;
    pending = null;
    setStatus("Waiting for the controller.");
  }
  const values = device.values || {},
    reported = values.params,
    reachable = device.connection !== "offline";
  if (reported && !edited) for (const p of PARAMS) if (reported[p.key] !== undefined) show(p, reported[p.key]);
  const inUse = reported
    ? `${values.params_source === "dashboard" ? "set from the dashboard" : "menuconfig"}, ` +
      `${changed(reported)} of ${PARAMS.length} differ from the menuconfig defaults`
    : "not reported (controller firmware without these settings)";
  if (pending) {
    if (answered(values)) {
      pending = null;
      edited = false;
      setStatus(`Applied and saved on the controller: ${inUse}.`, "good");
    } else if (Date.now() - pending.sentAt > ANSWER_WAIT_MS) {
      pending = null;
      setStatus(
        "The controller didn't report the change. It refuses settings that don't fit together (for example a " +
          "shutdown that would come before its buzzer) and needs \"Accept the other safety settings from the " +
          'dashboard" on in menuconfig.',
        "bad",
      );
    } else {
      setStatus(`Sent (request ${pending.request_id}), waiting for the controller…`, "warn");
    }
  } else if (!reachable) {
    setStatus(`Controller offline. Last known: ${inUse}.`, "warn");
  } else if (sending || ["bad", "good"].some((style) => $("pr-status").classList.contains(style))) {
    // keep "sending…", the last error or the last result until the next click or edit
  } else if (edited) {
    setStatus(`Not applied yet. In use: ${inUse}.`, "warn");
  } else {
    setStatus(`In use: ${inUse}.`, values.params_source === "dashboard" ? "good" : "");
  }
  const usable = Boolean(reported) && reachable && pending === null && !sending;
  for (const p of PARAMS) $(inputId(p)).disabled = !usable;
  $("pr-apply").disabled = !usable;
  $("pr-defaults").disabled = !usable;
}

async function send(body, expect) {
  sending = true;
  $("pr-apply").disabled = true;
  $("pr-defaults").disabled = true;
  setStatus("Sending…", "warn");
  try {
    const result = await sendParams(deviceId, body);
    pending = { request_id: result.request_id, sentAt: Date.now(), expect };
    setStatus(`Sent (request ${result.request_id}), waiting for the controller…`, "warn");
  } catch (error) {
    pending = null;
    $("pr-apply").disabled = false;
    $("pr-defaults").disabled = false;
    setStatus(`Not sent: ${error.message}`, "bad");
  } finally {
    sending = false;
  }
}

build();

$("pr-apply").addEventListener("click", () => {
  const body = {};
  for (const p of PARAMS) {
    const v = read(p);
    if (v === null) {
      setStatus(`${p.label} must be ${paramText(p, p.min)} to ${paramText(p, p.max)}.`, "bad");
      return;
    }
    body[p.key] = v;
  }
  if (body.node_stale_ms >= body.node_offline_ms) {
    setStatus("A sensor node must go STALE before it goes OFFLINE.", "bad");
    return;
  }
  send(body, body);
});

$("pr-defaults").addEventListener("click", () => {
  edited = false;
  send({ defaults: true }, "menuconfig");
});
