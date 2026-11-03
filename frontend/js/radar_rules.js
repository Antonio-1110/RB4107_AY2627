"use strict";
// The radar state and distance rules under the Person card's More settings (Tuning > Safety): off / observe /
// enforce and the numbers behind "passing" and "cooking". Saved on the controller; the answer is
// values.radar_rules in its telemetry (a placeholder, not tuned on real walks: docs/safety_state_machine.md).
// Saved with its card's Apply (safety_save.js).

import { sendRadarRules } from "./api.js";
import { $ } from "./dom.js";
import { number } from "./format.js";
import { registerPart, report, showsResult } from "./safety_save.js";

const ANSWER_WAIT_MS = 15000;
const LABEL = "Radar classification rules";
const FIELDS = [
  ["rr-gap", "quiet_gap_s", "quiet_gap_seconds", 0, 10],
  ["rr-dwell", "dwell_s", "dwell_seconds", 0, 60],
  ["rr-still", "stationary_min_s", "stationary_min_seconds", 0, 60],
  ["rr-window", "settle_window_s", "settle_window_seconds", 0.5, 60],
  ["rr-span", "settle_span_cm", "settle_span_cm", 0, 1000],
  ["rr-zone", "zone_max_cm", "zone_max_cm", 0, 1200],
];
const INPUTS = ["rr-mode", ...FIELDS.map(([id]) => id)];

let deviceId = "",
  edited = false,
  sending = false,
  usable = false,
  pending = null; // {request_id, sentAt, expect: the body sent, or "menuconfig"}

const setStatus = (text, style = "") => report(LABEL, text, style);

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
    setStatus("");
  }
  const r = reported(device.values || {}),
    reachable = device.connection !== "offline";
  if (r && !edited) {
    $("rr-mode").value = r.mode;
    for (const [id, , field] of FIELDS) $(id).value = number(r[field]) ? String(r[field]) : "";
  }
  if (pending) {
    if (r && answered(r)) {
      pending = null;
      edited = false;
      setStatus(`saved, ${r.mode}.`, "good");
    } else if (Date.now() - pending.sentAt > ANSWER_WAIT_MS) {
      pending = null;
      setStatus(
        "the controller did not confirm the change. Check that its firmware supports this setting and that " +
          '"Accept the radar state and distance rules from the dashboard" is enabled in menuconfig.',
        "bad",
      );
    }
  } else if (!sending && !showsResult(LABEL)) {
    setStatus("");
  }
  usable = Boolean(r) && reachable && pending === null && !sending;
  for (const id of INPUTS) $(id).disabled = !usable;
}

async function send(body, expect) {
  sending = true;
  setStatus("sending…", "warn");
  try {
    const result = await sendRadarRules(deviceId, body);
    pending = { request_id: result.request_id, sentAt: Date.now(), expect };
    setStatus(`sent (request ${result.request_id}); waiting for the controller to confirm…`, "warn");
  } catch (error) {
    pending = null;
    setStatus(`not sent: ${error.message}`, "bad");
  } finally {
    sending = false;
  }
}

for (const id of INPUTS)
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
    const body = { mode: $("rr-mode").value };
    for (const [id, key, , low, high] of FIELDS) {
      const value = Number($(id).value);
      if ($(id).value === "" || !Number.isFinite(value) || value < low || value > high)
        return { error: `${$(id).closest("label").firstChild.textContent.trim()} must be ${low}–${high}.` };
      body[key] = value;
    }
    return { run: () => send(body, body) };
  },
  defaults() {
    edited = false;
    send({ defaults: true }, "menuconfig");
  },
});
