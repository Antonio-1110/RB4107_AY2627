"use strict";
// Radar tuning panel: reads a presence node's C4002 settings from the device's
// values (sensors.<node>.c4002) and sends tuning commands (docs/c4002_tuning.md).
// The form is only refilled from the node when nobody is editing it.
// The disappear delay, lock time and report period are fixed on the node, and
// the per-gate thresholds come from "Calibrate", so the panel doesn't set them
// (the gate table shows the thresholds read-only).
// "All radars" sends every command to each presence node; the form, gate table
// and live view then show the first node, and the status lists each node's answer.

import { sendC4002Command } from "./api.js";
import { $, make } from "./dom.js";
import { markSettingsChange, selectLiveNode } from "./live_radar.js";

const GATES = 25;
const ANSWER_WAIT_MS = 12000;
const ALL = "*"; // the "All radars" choice

let deviceId = "",
  node = "", // the chosen node, or ALL
  nodes = [],
  shownKey = "",
  formDirty = false,
  gatesDirty = false,
  pending = {}, // node -> {request_id, action, sentAt}
  errors = {}, // node -> why the last command was not sent
  formError = "", // a form problem found before sending
  latest = null, // sensors.<view node>.c4002 as last rendered
  reports = {}; // node -> sensors.<node>.c4002 as last rendered

const FIELDS = ["tune-range-min", "tune-range-max", "tune-resolution", "tune-motion-sens",
  "tune-presence-sens"];
const BUTTONS = ["tune-apply", "tune-read", "tune-reset", "tune-calibrate", "tune-apply-gates"];

function presenceNodes(device) {
  return Object.entries(device.values.sensors || {})
    .filter(([, sensor]) => sensor && (sensor.role === "presence" || sensor.type === "C4002"))
    .map(([id]) => id)
    .sort();
}

// The nodes a command goes to, and the node the form and live view show.
const targets = () => (node === ALL ? nodes : node ? [node] : []);
const viewNode = () => (node === ALL ? nodes[0] || "" : node);

function setStatus(text, style = "") {
  const status = $("tuning-status");
  status.textContent = text;
  status.className = `tuning-status ${style || "muted"}`;
}

const RESULT_TEXT = {
  invalid: "the node rejected a value as out of range",
  sensor_error: "the C4002 radar did not acknowledge the command (check its UART wiring)",
  rejected: "the controller rejected the command",
  unknown_node: "the controller has received nothing from this node since it started",
  send_failed: "the controller could not send the command over ESP-NOW",
  no_reply: "the node did not reply",
};

function describe(tuning) {
  if (!tuning) return ["Waiting for this node's settings (the controller requests them when it first receives a packet from the node).", ""];
  const what = tuning.request_id === 0 ? "Settings report" : `Last command (${tuning.action || "?"})`;
  if (tuning.result !== "ok") {
    const reason = RESULT_TEXT[tuning.result] || tuning.result;
    return [`${what}: ${reason}${tuning.error ? ` · ${tuning.error}` : ""}.`, "bad"];
  }
  if (tuning.calibration_remaining_s > 0)
    return [`Calibrating: keep the area empty; approx. ${tuning.calibration_remaining_s} s remaining (as of the node's last report).`, "warn"];
  const saved = tuning.saved ? "saved on the node" : "not saved on the node (menuconfig values, or lost on restart)";
  return [`${what}: OK. Settings in use, ${saved}.`, "good"];
}

function fillForm(s) {
  $("tune-range-min").value = s.range_min_cm;
  $("tune-range-max").value = s.range_max_cm;
  $("tune-resolution").value = String(s.resolution_cm);
  $("tune-motion-sens").value = s.motion_sensitivity;
  $("tune-presence-sens").value = s.presence_sensitivity;
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
    for (const key of ["motion_thresholds", "presence_thresholds"])
      row.insertCell().textContent = settings && settings[key] ? String(settings[key][i]) : "—";
  }
  gatesDirty = false;
}

// One node's status line: its pending command, a send error, or its last report.
function nodeStatus(id) {
  const sent = pending[id];
  if (sent) {
    const waited = Date.now() - sent.sentAt;
    return waited < ANSWER_WAIT_MS
      ? [`Sent ${sent.action} to ${id}; waiting for the node's reply…`, "warn"]
      : [`Sent ${sent.action} to ${id}; no reply yet. Check that the controller is connected to MQTT.`, "warn"];
  }
  if (errors[id]) return [errors[id], "bad"];
  const [text, style] = describe(reports[id]);
  return [node === ALL ? `${id}: ${text}` : text, style];
}

// Settings the form sets, to spot nodes that are tuned differently.
const FORM_KEYS = ["range_min_cm", "range_max_cm", "resolution_cm", "motion_sensitivity",
  "presence_sensitivity", "motion_gates", "presence_gates"];
const formPart = (id) => {
  const s = reports[id] && reports[id].settings;
  return s ? JSON.stringify(FORM_KEYS.map((k) => s[k])) : "";
};

export function renderTuning(device) {
  const list = presenceNodes(device);
  if (device.device_id !== deviceId || list.join() !== nodes.join()) {
    const changedDevice = device.device_id !== deviceId;
    deviceId = device.device_id;
    nodes = list;
    const select = $("tuning-node");
    const options = list.map((id) => new Option(id, id));
    if (list.length > 1) options.unshift(new Option(`All presence nodes (${list.length})`, ALL));
    select.replaceChildren(...options);
    if (changedDevice || !(node === ALL ? list.length > 1 : list.includes(node))) {
      node = list.length > 1 ? ALL : list[0] || "";
      shownKey = "";
      pending = {};
      errors = {};
    }
    select.value = node;
  }
  const shown = viewNode();
  $("tuning-form").disabled = !node;
  for (const id of BUTTONS) $(id).disabled = !node;
  reports = {};
  for (const id of nodes) reports[id] = (device.values.sensors[id] || {}).c4002 || null;
  latest = shown ? reports[shown] : null;
  selectLiveNode(deviceId, shown, latest && latest.settings);
  $("live-title").textContent = shown ? `Radar detections from ${shown}, last 60 s` : "Radar detections, last 60 s";
  if (!node) {
    setStatus("No presence node has reported.");
    return;
  }

  let answered = false;
  for (const id of Object.keys(pending)) {
    const report = reports[id];
    if (report && report.request_id === pending[id].request_id) {
      if (id === shown && report.result === "ok" && pending[id].action !== "read")
        markSettingsChange(pending[id].action);
      delete pending[id];
      answered = true;
    }
  }
  if (answered && !Object.keys(pending).length) {
    formDirty = false; // show what the node now uses
    gatesDirty = false;
  }
  const lines = targets().map(nodeStatus);
  if (formError) lines.unshift([formError, "bad"]);
  if (node === ALL) {
    const parts = nodes.map(formPart);
    if (parts.every(Boolean) && new Set(parts).size > 1)
      lines.push([`The presence nodes have different settings; the form shows ${shown}'s, and Apply sends them to every node.`, "warn"]);
  }
  const rank = { bad: 3, warn: 2, good: 1, "": 0 };
  const style = lines.reduce((worst, [, s]) => (rank[s] > rank[worst] ? s : worst), "");
  setStatus(lines.map(([text]) => text).join("\n"), style);

  const settings = latest && latest.settings;
  const key = settings ? `${deviceId}|${shown}|${latest.reported_at}|${latest.request_id}|${JSON.stringify(settings)}` : "";
  if (settings && key !== shownKey) {
    if (!formDirty) fillForm(settings);
    if (!gatesDirty) renderGateRows(settings);
    shownKey = key;
  } else if (!settings && !$("tune-gate-rows").rows.length) {
    renderGateRows(null);
  }
}

// Sends the command to every target node at once; each node answers on its own.
async function send(body) {
  const list = targets();
  if (!deviceId || !list.length) return;
  formError = "";
  await Promise.all(
    list.map(async (id) => {
      delete errors[id];
      delete pending[id];
      try {
        const result = await sendC4002Command(deviceId, id, body);
        pending[id] = { request_id: result.request_id, action: body.action, sentAt: Date.now() };
      } catch (error) {
        errors[id] = `${id}: not sent: ${error.message}`;
      }
    }),
  );
  const lines = list.map(nodeStatus);
  setStatus(lines.map(([text]) => text).join("\n"), lines.some(([, s]) => s === "bad") ? "bad" : "warn");
}

// "node_01" or "every presence node (node_01, node_02)", for confirmations.
const targetName = () => (node === ALL ? `every presence node (${nodes.join(", ")})` : node);

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
  pending = {};
  errors = {};
  formError = "";
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
  }),
);
$("tune-read").addEventListener("click", () => send({ action: "read" }));
$("tune-reset").addEventListener("click", () => {
  if (confirm(`Erase the settings saved on ${targetName()} and restore the menuconfig defaults?`))
    send({ action: "reset" });
});
$("tune-calibrate").addEventListener("click", () => {
  const delay = intValue("tune-calib-delay"),
    duration = intValue("tune-calib-duration");
  if (
    confirm(
      `${targetName()} will calibrate the empty area for ${duration} s, starting in ${delay} s. ` +
        "Everyone must stay out of the radar's field of view, and moving objects (such as fans) must be in their normal state. " +
        "The node then keeps the learned thresholds and both sensitivities change to Custom. Start calibration?",
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
