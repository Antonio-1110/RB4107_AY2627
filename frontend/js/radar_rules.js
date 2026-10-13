"use strict";
// The radar state and distance rules in the Person rule (Tuning > Rules): off / observe / enforce and the
// numbers behind "passing" and "dwelling". Saved on the controller; the answer is values.radar_rules in its
// telemetry (a placeholder, not tuned on real walks: docs/safety_state_machine.md).

import { sendRadarRules } from "./api.js";
import { $ } from "./dom.js";
import { number } from "./format.js";

const ANSWER_WAIT_MS = 15000;
const FIELDS = [
  ["rr-gap", "quiet_gap_s", "quiet_gap_seconds", 0, 10],
  ["rr-dwell", "dwell_s", "dwell_seconds", 0, 60],
  ["rr-still", "stationary_min_s", "stationary_min_seconds", 0, 60],
  ["rr-window", "settle_window_s", "settle_window_seconds", 0.5, 60],
  ["rr-span", "settle_span_cm", "settle_span_cm", 0, 1000],
  ["rr-zone", "zone_max_cm", "zone_max_cm", 0, 1200],
];

let deviceId = "",
  edited = false,
  sending = false,
  pending = null; // {request_id, sentAt, expect: the body sent, or "menuconfig"}

function setStatus(text, style = "") {
  const status = $("rr-status");
  status.textContent = text;
  status.className = `tuning-status ${style || "muted"}`;
}

const describe = (r) =>
  `${r.mode}, dwelling after ${r.dwell_seconds} s (${r.stationary_min_seconds} s still, within ${r.settle_span_cm} cm) ` +
  `(${r.source === "dashboard" ? "set from the dashboard" : "menuconfig"})`;

const reported = (values) => (values.radar_rules && values.radar_rules.quiet_gap_seconds !== undefined ? values.radar_rules : null);

function answered(r) {
  if (pending.expect === "menuconfig") return r.source === "menuconfig";
  return (
    r.source === "dashboard" &&
    r.mode === pending.expect.mode &&
    FIELDS.every(([, key, field]) => Math.abs(r[field] - pending.expect[key]) < 0.05)
  );
}

export function renderRadarRules(device) {
  if (!device) return;
  if (device.device_id !== deviceId) {
    deviceId = device.device_id;
    edited = false;
    pending = null;
    setStatus("Waiting for the controller.");
  }
  const r = reported(device.values || {}),
    reachable = device.connection !== "offline";
  if (r && !edited) {
    $("rr-mode").value = r.mode;
    for (const [id, , field] of FIELDS) $(id).value = number(r[field]) ? String(r[field]) : "";
  }
  const inUse = r ? describe(r) : "not reported (controller firmware without these rules?)";
  if (pending) {
    if (r && answered(r)) {
      pending = null;
      edited = false;
      setStatus(`Applied and saved on the controller: ${inUse}.`, "good");
    } else if (Date.now() - pending.sentAt > ANSWER_WAIT_MS) {
      pending = null;
      setStatus(
        "The controller didn't report the change. Check that it runs firmware with this feature and that " +
          '"Accept the radar state and distance rules from the dashboard" is on in menuconfig.',
        "bad",
      );
    } else {
      setStatus(`Sent (request ${pending.request_id}), waiting for the controller…`, "warn");
    }
  } else if (!reachable) {
    setStatus(`Controller offline. Last known: ${inUse}.`, "warn");
  } else if (sending || ["bad", "good"].some((style) => $("rr-status").classList.contains(style))) {
    // keep "sending…", the last error or the last result until the next click or edit
  } else if (edited) {
    setStatus(`Not applied yet. In use: ${inUse}.`, "warn");
  } else {
    setStatus(`In use: ${inUse}.`, r && r.source === "dashboard" ? "good" : "");
  }
  const usable = Boolean(r) && reachable && pending === null && !sending;
  for (const id of ["rr-mode", ...FIELDS.map(([id]) => id), "rr-apply", "rr-defaults"]) $(id).disabled = !usable;
}

async function send(body, expect) {
  sending = true;
  $("rr-apply").disabled = true;
  $("rr-defaults").disabled = true;
  setStatus("Sending…", "warn");
  try {
    const result = await sendRadarRules(deviceId, body);
    pending = { request_id: result.request_id, sentAt: Date.now(), expect };
    setStatus(`Sent (request ${result.request_id}), waiting for the controller…`, "warn");
  } catch (error) {
    pending = null;
    $("rr-apply").disabled = false;
    $("rr-defaults").disabled = false;
    setStatus(`Not sent: ${error.message}`, "bad");
  } finally {
    sending = false;
  }
}

for (const id of ["rr-mode", ...FIELDS.map(([id]) => id)])
  $(id).addEventListener("input", () => {
    edited = true;
    setStatus("Not applied yet.", "warn");
  });

$("rr-apply").addEventListener("click", () => {
  const body = { mode: $("rr-mode").value };
  for (const [id, key, , low, high] of FIELDS) {
    const value = Number($(id).value);
    if ($(id).value === "" || !Number.isFinite(value) || value < low || value > high) {
      setStatus(`${$(id).closest("label").firstChild.textContent.trim()} must be ${low}–${high}.`, "bad");
      return;
    }
    body[key] = value;
  }
  send(body, body);
});

$("rr-defaults").addEventListener("click", () => {
  edited = false;
  send({ defaults: true }, "menuconfig");
});
