"use strict";
// Temperature tiers in the Timer rule (Tuning > Rules): the hotter the pan, the shorter the buzzer and
// shutdown times (docs/safety_state_machine.md). The controller checks the values, saves them and
// answers with tiers / tiers_source in its telemetry.

import { sendTiers } from "./api.js";
import { $ } from "./dom.js";
import { number } from "./format.js";

const ANSWER_WAIT_MS = 15000;
// [input id, group, key in the telemetry's seconds form, key sent]
const FIELDS = [
  ["tr-e-temp", "elevated", "temp_c", "temp_c"],
  ["tr-e-warn", "elevated", "warning_seconds", "warning_s"],
  ["tr-e-stop", "elevated", "shutdown_seconds", "shutdown_s"],
  ["tr-c-temp", "critical", "temp_c", "temp_c"],
  ["tr-c-rate", "critical", "rate_c_per_min", "rate_c_per_min"],
  ["tr-c-ratemin", "critical", "rate_min_temp_c", "rate_min_temp_c"],
  ["tr-c-warn", "critical", "warning_seconds", "warning_s"],
  ["tr-c-stop", "critical", "shutdown_seconds", "shutdown_s"],
];

let deviceId = "",
  edited = false,
  sending = false,
  pending = null, // {request_id, sentAt, expect: {id: value} or "menuconfig"}
  countsFrom = null; // "UNATTENDED" or "WARNING"

function setStatus(text, style = "") {
  const status = $("tr-status");
  status.textContent = text;
  status.className = `tuning-status ${style || "muted"}`;
}

const describe = (t, source) =>
  `elevated from ${t.elevated.temp_c} °C: ${t.elevated.warning_seconds}/${t.elevated.shutdown_seconds} s; ` +
  `critical from ${t.critical.temp_c} °C: ${t.critical.warning_seconds}/${t.critical.shutdown_seconds} s ` +
  `(${source === "dashboard" ? "set from the dashboard" : "menuconfig"})`;

function answered(values) {
  if (pending.expect === "menuconfig") return values.tiers_source === "menuconfig";
  if (values.tiers_source !== "dashboard" || !values.tiers) return false;
  return FIELDS.every(([id, group, key]) => Math.abs(values.tiers[group][key] - pending.expect[id]) < 0.06);
}

export function renderTiers(device) {
  if (!device) return;
  if (device.device_id !== deviceId) {
    deviceId = device.device_id;
    edited = false;
    pending = null;
    setStatus("Waiting for the controller.");
  }
  const values = device.values || {},
    tiers = values.tiers,
    reachable = device.connection !== "offline";
  countsFrom = values.shutdown_counts_from;
  if (tiers && !edited) {
    for (const [id, group, key] of FIELDS) {
      const v = tiers[group][key];
      $(id).value = number(v) ? String(Math.round(v * 10) / 10) : "";
    }
  }
  const inUse = tiers ? describe(tiers, values.tiers_source) : "not reported (controller firmware without tiers, or tiers off)";
  if (pending) {
    if (answered(values)) {
      pending = null;
      edited = false;
      setStatus(`Applied and saved on the controller: ${inUse}.`, "good");
    } else if (Date.now() - pending.sentAt > ANSWER_WAIT_MS) {
      pending = null;
      setStatus(
        "The controller didn't report the change. It refuses inconsistent values (a shutdown not after its warning, critical below elevated) " +
          'and needs "Accept the temperature tiers from the dashboard" on in menuconfig.',
        "bad",
      );
    } else {
      setStatus(`Sent (request ${pending.request_id}), waiting for the controller…`, "warn");
    }
  } else if (!reachable) {
    setStatus(`Controller offline. Last known: ${inUse}.`, "warn");
  } else if (sending || ["bad", "good"].some((style) => $("tr-status").classList.contains(style))) {
    // keep "sending…", the last error or the last result until the next click or edit
  } else if (edited) {
    setStatus(`Not applied yet. In use: ${inUse}.`, "warn");
  } else {
    setStatus(`In use: ${inUse}.`, values.tiers_source === "dashboard" ? "good" : "");
  }
  const usable = Boolean(tiers) && reachable && pending === null && !sending;
  for (const [id] of FIELDS) $(id).disabled = !usable;
  $("tr-apply").disabled = !usable;
  $("tr-defaults").disabled = !usable;
}

async function send(body, expect) {
  sending = true;
  $("tr-apply").disabled = true;
  $("tr-defaults").disabled = true;
  setStatus("Sending…", "warn");
  try {
    const result = await sendTiers(deviceId, body);
    pending = { request_id: result.request_id, sentAt: Date.now(), expect };
    setStatus(`Sent (request ${result.request_id}), waiting for the controller…`, "warn");
  } catch (error) {
    pending = null;
    $("tr-apply").disabled = false;
    $("tr-defaults").disabled = false;
    setStatus(`Not sent: ${error.message}`, "bad");
  } finally {
    sending = false;
  }
}

for (const [id] of FIELDS)
  $(id).addEventListener("input", () => {
    edited = true;
    setStatus("Not applied yet.", "warn");
  });

$("tr-apply").addEventListener("click", () => {
  const body = { elevated: {}, critical: {} },
    expect = {};
  for (const [id, group, , sentKey] of FIELDS) {
    const input = $(id),
      value = Number(input.value);
    if (input.value === "" || !Number.isFinite(value) || value < Number(input.min) || value > Number(input.max)) {
      setStatus(`${input.parentElement.firstChild.textContent.trim()} (${group}) must be ${input.min}–${input.max}.`, "bad");
      return;
    }
    body[group][sentKey] = value;
    expect[id] = value;
  }
  const { elevated: e, critical: c } = body;
  if (e.temp_c > 0 && c.temp_c > 0 && c.temp_c < e.temp_c) {
    setStatus("Critical must not start below elevated.", "bad");
    return;
  }
  if (countsFrom === "UNATTENDED" && (e.shutdown_s <= e.warning_s || c.shutdown_s <= c.warning_s)) {
    setStatus("In each tier the shutdown must come after the buzzer (both count from when the person left).", "bad");
    return;
  }
  send(body, expect);
});

$("tr-defaults").addEventListener("click", () => {
  edited = false;
  send({ defaults: true }, "menuconfig");
});
