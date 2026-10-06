"use strict";
// Radar tuning panel: reads a presence node's C4002 settings from the device's
// values (sensors.<node>.c4002) and sends tuning commands (docs/c4002_tuning.md).
// The form is only refilled from the node when nobody is editing it.

import { sendC4002Command } from "./api.js";
import { $, make } from "./dom.js";
import { markSettingsChange, selectLiveNode } from "./live_radar.js";

const GATES = 25;
const ANSWER_WAIT_MS = 12000;

let deviceId = "",
  node = "",
  nodes = [],
  shownKey = "",
  formDirty = false,
  gatesDirty = false,
  pending = null, // {request_id, action, sentAt}
  lastError = "",
  latest = null; // sensors.<node>.c4002 as last rendered

const FIELDS = ["tune-range-min", "tune-range-max", "tune-resolution", "tune-motion-sens",
  "tune-presence-sens", "tune-hold", "tune-lock", "tune-period"];
const BUTTONS = ["tune-apply", "tune-read", "tune-reset", "tune-calibrate", "tune-apply-gates",
  "tune-apply-thresholds"];

function presenceNodes(device) {
  return Object.entries(device.values.sensors || {})
    .filter(([, sensor]) => sensor && (sensor.role === "presence" || sensor.type === "C4002"))
    .map(([id]) => id)
    .sort();
}

function setStatus(text, style = "") {
  const status = $("tuning-status");
  status.textContent = text;
  status.className = `tuning-status ${style || "muted"}`;
}

const RESULT_TEXT = {
  invalid: "the node refused a value",
  sensor_error: "the C4002 did not accept a command (wiring?)",
  rejected: "the controller rejected the command",
  unknown_node: "the controller has not heard from this node since it started",
  send_failed: "the controller could not send it over ESP-NOW",
  no_reply: "the node did not answer",
};

function describe(tuning) {
  if (!tuning) return ["Waiting for this node's settings (the controller asks for them when it first hears the node).", ""];
  const what = tuning.request_id === 0 ? "Settings report" : `Last command (${tuning.action || "?"})`;
  if (tuning.result !== "ok") {
    const reason = RESULT_TEXT[tuning.result] || tuning.result;
    return [`${what}: ${reason}${tuning.error ? ` · ${tuning.error}` : ""}.`, "bad"];
  }
  if (tuning.calibration_remaining_s > 0)
    return [`Calibrating: keep the area empty, about ${tuning.calibration_remaining_s} s left (counted from the node's last report).`, "warn"];
  const saved = tuning.saved ? "saved on the node" : "not saved (menuconfig values, or lost at reboot)";
  return [`${what}: OK. Settings in use, ${saved}.`, "good"];
}

function fillForm(s) {
  $("tune-range-min").value = s.range_min_cm;
  $("tune-range-max").value = s.range_max_cm;
  $("tune-resolution").value = String(s.resolution_cm);
  $("tune-motion-sens").value = s.motion_sensitivity;
  $("tune-presence-sens").value = s.presence_sensitivity;
  $("tune-hold").value = s.disappear_delay_s;
  $("tune-lock").value = (s.lock_time_ds / 10).toFixed(1);
  $("tune-period").value = (s.report_period_ds / 10).toFixed(1);
  formDirty = false;
}

function gateCount() {
  return $("tune-resolution").value === "20" ? 25 : 15;
}

function renderGateRows(settings) {
  const rows = $("tune-gate-rows");
  rows.replaceChildren();
  const count = gateCount(),
    size = count === 25 ? 20 : 80;
  for (let i = 0; i < count; i++) {
    const row = rows.insertRow();
    row.insertCell().textContent = String(i);
    row.insertCell().textContent = `${i * size}–${(i + 1) * size} cm`;
    for (const [kind, key] of [["motion-on", "motion_gates"], ["still-on", "presence_gates"]]) {
      const box = make("input");
      box.type = "checkbox";
      box.id = `tune-${kind}-${i}`;
      box.checked = settings ? settings[key][i] === 1 : true;
      row.insertCell().append(box);
    }
    for (const [kind, key] of [["motion-thr", "motion_thresholds"], ["still-thr", "presence_thresholds"]]) {
      const input = make("input");
      input.type = "number";
      input.min = "0";
      input.max = "99";
      input.id = `tune-${kind}-${i}`;
      input.placeholder = "?";
      if (settings && settings[key]) input.value = settings[key][i];
      row.insertCell().append(input);
    }
  }
  gatesDirty = false;
}

export function renderTuning(device) {
  const list = presenceNodes(device);
  if (device.device_id !== deviceId || list.join() !== nodes.join()) {
    deviceId = device.device_id;
    nodes = list;
    const select = $("tuning-node");
    select.replaceChildren(...list.map((id) => new Option(id, id)));
    if (!list.includes(node)) {
      node = list[0] || "";
      shownKey = "";
      pending = null;
    }
    select.value = node;
  }
  $("tuning-form").disabled = !node;
  for (const id of BUTTONS) $(id).disabled = !node;
  latest = node ? (device.values.sensors[node] || {}).c4002 || null : null;
  selectLiveNode(deviceId, node, latest && latest.settings);
  if (!node) {
    setStatus("No presence node reported yet.");
    return;
  }

  if (pending && latest && latest.request_id === pending.request_id) {
    if (latest.result === "ok" && pending.action !== "read") markSettingsChange(pending.action);
    pending = null;
    formDirty = false; // show what the node now uses
    gatesDirty = false;
  }
  if (pending) {
    const waited = Date.now() - pending.sentAt;
    setStatus(
      waited < ANSWER_WAIT_MS
        ? `Sent ${pending.action} to ${node}, waiting for the node to answer…`
        : `Sent ${pending.action} to ${node}, but no answer yet. Is the controller connected to MQTT?`,
      "warn",
    );
  } else if (lastError) {
    setStatus(lastError, "bad");
  } else {
    setStatus(...describe(latest));
  }

  const settings = latest && latest.settings;
  const key = settings ? `${deviceId}|${node}|${latest.reported_at}|${latest.request_id}|${JSON.stringify(settings)}` : "";
  if (settings && key !== shownKey) {
    if (!formDirty) fillForm(settings);
    if (!gatesDirty) renderGateRows(settings);
    shownKey = key;
  } else if (!settings && !$("tune-gate-rows").rows.length) {
    renderGateRows(null);
  }
}

async function send(body) {
  if (!deviceId || !node) return;
  lastError = "";
  try {
    const result = await sendC4002Command(deviceId, node, body);
    pending = { request_id: result.request_id, action: body.action, sentAt: Date.now() };
    setStatus(`Sent ${body.action} to ${node}, waiting for the node to answer…`, "warn");
  } catch (error) {
    lastError = `Not sent: ${error.message}`;
    setStatus(lastError, "bad");
  }
}

function intValue(id, scale = 1) {
  const value = Number($(id).value);
  return Math.round(value * scale);
}

function currentArray(key, fallback) {
  const settings = latest && latest.settings;
  return settings && settings[key] ? [...settings[key]] : Array(GATES).fill(fallback);
}

$("tuning-node").addEventListener("change", () => {
  node = $("tuning-node").value;
  shownKey = "";
  formDirty = false;
  gatesDirty = false;
  pending = null;
  lastError = "";
  $("tune-gate-rows").replaceChildren();
});
for (const id of FIELDS) $(id).addEventListener("input", () => (formDirty = true));
$("tune-resolution").addEventListener("change", () => renderGateRows(latest && latest.settings));
$("tune-gate-rows").addEventListener("input", () => (gatesDirty = true));

$("tune-apply").addEventListener("click", () =>
  send({
    action: "apply",
    range_min_cm: intValue("tune-range-min"),
    range_max_cm: intValue("tune-range-max"),
    resolution_cm: Number($("tune-resolution").value),
    motion_sensitivity: $("tune-motion-sens").value,
    presence_sensitivity: $("tune-presence-sens").value,
    disappear_delay_s: intValue("tune-hold"),
    lock_time_ds: intValue("tune-lock", 10),
    report_period_ds: intValue("tune-period", 10),
  }),
);
$("tune-read").addEventListener("click", () => send({ action: "read" }));
$("tune-reset").addEventListener("click", () => {
  if (confirm(`Forget the settings saved on ${node} and go back to its menuconfig values?`))
    send({ action: "reset" });
});
$("tune-calibrate").addEventListener("click", () => {
  const delay = intValue("tune-calib-delay"),
    duration = intValue("tune-calib-duration");
  if (
    confirm(
      `${node} will learn the empty room for ${duration} s, starting in ${delay} s. ` +
        "Everyone must leave the radar's view, and moving things (fans) should be in their normal state. " +
        "Afterwards the node keeps the learned thresholds (sensitivity becomes custom). Start?",
    )
  )
    send({ action: "calibrate", calibration_delay_s: delay, calibration_duration_s: duration });
});
$("tune-apply-gates").addEventListener("click", () => {
  const motion = currentArray("motion_gates", 1),
    still = currentArray("presence_gates", 1);
  for (let i = 0; i < gateCount(); i++) {
    motion[i] = $(`tune-motion-on-${i}`).checked ? 1 : 0;
    still[i] = $(`tune-still-on-${i}`).checked ? 1 : 0;
  }
  send({ action: "apply", motion_gates: motion, presence_gates: still });
});
$("tune-apply-thresholds").addEventListener("click", () => {
  const motion = currentArray("motion_thresholds", 99),
    still = currentArray("presence_thresholds", 99);
  for (let i = 0; i < gateCount(); i++) {
    const m = $(`tune-motion-thr-${i}`).value,
      s = $(`tune-still-thr-${i}`).value;
    if (m === "" || s === "") {
      lastError = `Fill in every threshold first (gate ${i} is empty), or press "Read from sensor".`;
      setStatus(lastError, "bad");
      return;
    }
    motion[i] = Number(m);
    still[i] = Number(s);
  }
  send({ action: "apply", motion_thresholds: motion, presence_thresholds: still });
});
