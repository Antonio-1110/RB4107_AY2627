"use strict";
// Attendance in the Person card (Tuning > Safety): who counts as at the stove (every working radar, or either)
// and how long someone passing may stop the countdown. The share of the shutdown time the pause may take is
// fixed on the dashboard, so the value in force is sent back unchanged. Saved on the controller; the answer is
// presence_filter.fusion / pause_budget_seconds / pause_budget_pct / attendance_source in its telemetry.
// Saved with its card's Apply (safety_save.js).

import { sendAttendance } from "./api.js";
import { $ } from "./dom.js";
import { number } from "./format.js";
import { registerPart, report, showsResult } from "./safety_save.js";

const ANSWER_WAIT_MS = 15000;
const LABEL = "Who is at the stove";
const FUSION_TEXT = { all: "every radar", any: "either radar" };

let deviceId = "",
  edited = false,
  sending = false,
  usable = false,
  inForce = null,
  pending = null; // {request_id, sentAt, expect: {budget, pct, fusion} or "menuconfig"}

const setStatus = (text, style = "") => report(LABEL, text, style);

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
    setStatus("");
  }
  const values = device.values || {},
    f = reported(values),
    reachable = device.connection !== "offline";
  inForce = f;
  if (f && !edited) {
    $("at-budget").value = number(f.pause_budget_seconds) ? String(f.pause_budget_seconds) : "";
    $("at-fusion").value = f.fusion;
  }
  if (pending) {
    if (f && answered(f)) {
      pending = null;
      edited = false;
      setStatus(
        `saved: at the stove when ${FUSION_TEXT[f.fusion] || f.fusion} sees them, a passer-by may pause the clock ` +
          `${f.pause_budget_seconds} s.`,
        "good",
      );
    } else if (Date.now() - pending.sentAt > ANSWER_WAIT_MS) {
      pending = null;
      setStatus(
        "the controller didn't report the change. Check that it runs firmware with this feature and that " +
          '"Accept the fusion and the pause budget from the dashboard" is on in menuconfig.',
        "bad",
      );
    }
  } else if (!sending && !showsResult(LABEL)) {
    setStatus("");
  }
  usable = Boolean(f) && reachable && pending === null && !sending;
  for (const id of ["at-budget", "at-fusion"]) $(id).disabled = !usable;
}

async function send(body, expect) {
  sending = true;
  setStatus("sending…", "warn");
  try {
    const result = await sendAttendance(deviceId, body);
    pending = { request_id: result.request_id, sentAt: Date.now(), expect };
    setStatus(`sent (request ${result.request_id}), waiting for the controller…`, "warn");
  } catch (error) {
    pending = null;
    setStatus(`not sent: ${error.message}`, "bad");
  } finally {
    sending = false;
  }
}

for (const id of ["at-budget", "at-fusion"])
  $(id).addEventListener("input", () => {
    edited = true;
    setStatus("");
  });

registerPart({
  label: LABEL,
  card: "person",
  edited: () => edited,
  busy: () => sending || pending !== null,
  usable: () => usable,
  prepare() {
    const budget = Number($("at-budget").value),
      pct = number(inForce.pause_budget_pct) ? Math.round(inForce.pause_budget_pct) : 25;
    if ($("at-budget").value === "" || !Number.isFinite(budget) || budget < 0 || budget > 600)
      return { error: "the pause for someone passing must be 0–600 s." };
    const fusion = $("at-fusion").value;
    return {
      run: () => send({ pause_budget_s: budget, pause_budget_pct: pct, fusion }, { budget, pct, fusion }),
    };
  },
  defaults() {
    edited = false;
    send({ defaults: true }, "menuconfig");
  },
});
