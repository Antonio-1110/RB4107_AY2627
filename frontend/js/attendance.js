"use strict";
// Attendance in the Person rule (Tuning > Rules): who counts as at the stove (every working radar, or either)
// and how long someone passing may stop the unattended clock. Saved on the controller; the answer is
// presence_filter.fusion / pause_budget_seconds / pause_budget_pct / attendance_source in its telemetry.

import { sendAttendance } from "./api.js";
import { $ } from "./dom.js";
import { number } from "./format.js";

const ANSWER_WAIT_MS = 15000;
const FUSION_TEXT = { all: "every working radar", any: "either radar" };

let deviceId = "",
  edited = false,
  sending = false,
  pending = null; // {request_id, sentAt, expect: {budget, pct, fusion} or "menuconfig"}

function setStatus(text, style = "") {
  const status = $("at-status");
  status.textContent = text;
  status.className = `tuning-status ${style || "muted"}`;
}

const describe = (f) =>
  `at the stove when ${FUSION_TEXT[f.fusion] || f.fusion} sees them; pause budget ${f.pause_budget_seconds} s / ${f.pause_budget_pct} % ` +
  `(${f.attendance_source === "dashboard" ? "set from the dashboard" : "menuconfig"})`;

const reported = (values) => {
  const f = values.presence_filter;
  return f && f.fusion ? f : null;
};

function answered(f) {
  if (pending.expect === "menuconfig") return f.attendance_source === "menuconfig";
  return (
    f.attendance_source === "dashboard" &&
    f.fusion === pending.expect.fusion &&
    Math.abs(f.pause_budget_seconds - pending.expect.budget) < 0.05 &&
    f.pause_budget_pct === pending.expect.pct
  );
}

export function renderAttendance(device) {
  if (!device) return;
  if (device.device_id !== deviceId) {
    deviceId = device.device_id;
    edited = false;
    pending = null;
    setStatus("Waiting for the controller.");
  }
  const values = device.values || {},
    f = reported(values),
    reachable = device.connection !== "offline";
  if (f && !edited) {
    $("at-budget").value = number(f.pause_budget_seconds) ? String(f.pause_budget_seconds) : "";
    $("at-pct").value = number(f.pause_budget_pct) ? String(f.pause_budget_pct) : "";
    $("at-fusion").value = f.fusion;
  }
  const inUse = f ? describe(f) : "not reported (controller firmware without attendance?)";
  if (pending) {
    if (f && answered(f)) {
      pending = null;
      edited = false;
      setStatus(`Applied and saved on the controller: ${inUse}.`, "good");
    } else if (Date.now() - pending.sentAt > ANSWER_WAIT_MS) {
      pending = null;
      setStatus(
        "The controller didn't report the change. Check that it runs firmware with this feature and that " +
          '"Accept the fusion and the pause budget from the dashboard" is on in menuconfig.',
        "bad",
      );
    } else {
      setStatus(`Sent (request ${pending.request_id}), waiting for the controller…`, "warn");
    }
  } else if (!reachable) {
    setStatus(`Controller offline. Last known: ${inUse}.`, "warn");
  } else if (sending || ["bad", "good"].some((style) => $("at-status").classList.contains(style))) {
    // keep "sending…", the last error or the last result until the next click or edit
  } else if (edited) {
    setStatus(`Not applied yet. In use: ${inUse}.`, "warn");
  } else {
    setStatus(`In use: ${inUse}.`, f && f.attendance_source === "dashboard" ? "good" : "");
  }
  const usable = Boolean(f) && reachable && pending === null && !sending;
  for (const id of ["at-budget", "at-pct", "at-fusion", "at-apply", "at-defaults"]) $(id).disabled = !usable;
}

async function send(body, expect) {
  sending = true;
  $("at-apply").disabled = true;
  $("at-defaults").disabled = true;
  setStatus("Sending…", "warn");
  try {
    const result = await sendAttendance(deviceId, body);
    pending = { request_id: result.request_id, sentAt: Date.now(), expect };
    setStatus(`Sent (request ${result.request_id}), waiting for the controller…`, "warn");
  } catch (error) {
    pending = null;
    $("at-apply").disabled = false;
    $("at-defaults").disabled = false;
    setStatus(`Not sent: ${error.message}`, "bad");
  } finally {
    sending = false;
  }
}

for (const id of ["at-budget", "at-pct", "at-fusion"])
  $(id).addEventListener("input", () => {
    edited = true;
    setStatus("Not applied yet.", "warn");
  });

$("at-apply").addEventListener("click", () => {
  const budget = Number($("at-budget").value),
    pct = Number($("at-pct").value);
  if ($("at-budget").value === "" || !Number.isFinite(budget) || budget < 0 || budget > 600) {
    setStatus("The pause budget must be 0–600 s.", "bad");
    return;
  }
  if ($("at-pct").value === "" || !Number.isFinite(pct) || pct < 0 || pct > 100) {
    setStatus("The share of the shutdown time must be 0–100 %.", "bad");
    return;
  }
  const fusion = $("at-fusion").value;
  send({ pause_budget_s: budget, pause_budget_pct: Math.round(pct), fusion }, { budget, pct: Math.round(pct), fusion });
});

$("at-defaults").addEventListener("click", () => {
  edited = false;
  send({ defaults: true }, "menuconfig");
});
