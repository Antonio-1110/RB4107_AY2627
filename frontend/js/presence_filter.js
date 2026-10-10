"use strict";
// Presence filter in the System status card (docs/remote_reset.md). Changes how
// long the radar must agree before the safety state machine counts "absent" or
// "returned", until the controller reboots. The controller's answer is
// safety.presence_filter in the next telemetry.

import { sendPresenceFilter } from "./api.js";
import { $ } from "./dom.js";
import { number } from "./format.js";

const ANSWER_WAIT_MS = 15000;
const FIELDS = [
  ["pf-absence", "absence_ms", "absence_seconds"],
  ["pf-return", "return_ms", "return_seconds"],
  ["pf-gap", "return_gap_ms", "return_gap_seconds"],
];

let deviceId = "",
  edited = false, // the user changed an input: don't overwrite it with telemetry
  sending = false, // request in flight
  pending = null; // {request_id, sentAt, expect: {key: seconds} or "menuconfig"}

function setStatus(text, style = "") {
  const status = $("pf-status");
  status.textContent = text;
  status.className = `tuning-status ${style || "muted"}`;
}

const describe = (filter) =>
  `absent after ${filter.absence_seconds} s, return after ${filter.return_seconds} s, ` +
  `gaps up to ${filter.return_gap_seconds} s ignored (${filter.source === "dashboard" ? "set here" : "menuconfig"})`;

function answered(filter) {
  if (!filter) return false;
  if (pending.expect === "menuconfig") return filter.source === "menuconfig";
  return filter.source === "dashboard" && FIELDS.every(([, , key]) => filter[key] === pending.expect[key]);
}

export function renderPresenceFilter(device) {
  if (device.device_id !== deviceId) {
    deviceId = device.device_id;
    edited = false;
    pending = null;
  }
  const filter = device.values.presence_filter,
    reachable = device.connection !== "offline";
  if (filter && !edited) {
    for (const [id, , key] of FIELDS) $(id).value = number(filter[key]) ? filter[key] : "";
  }
  const usable = Boolean(filter) && reachable && pending === null && !sending;
  $("presence-filter-form").disabled = !usable;
  $("pf-apply").disabled = !usable;
  $("pf-defaults").disabled = !usable;

  if (pending) {
    if (answered(filter)) {
      pending = null;
      edited = false;
      setStatus(`Applied: ${describe(filter)}.`, "good");
    } else if (Date.now() - pending.sentAt > ANSWER_WAIT_MS) {
      pending = null;
      setStatus(
        'No change from the controller. Check it is online and that "Accept presence filter settings from the dashboard" is on in menuconfig.',
        "bad",
      );
    } else {
      setStatus(`Sent (request ${pending.request_id}), waiting for the controller…`, "warn");
    }
  } else if (!filter) {
    setStatus("This controller's firmware does not report its presence filter yet.");
  } else if (!reachable) {
    setStatus(`Controller offline. Last known: ${describe(filter)}.`, "warn");
  } else if (sending || $("pf-status").classList.contains("bad")) {
    // keep "sending…" or the last error until the next click
  } else if (edited) {
    setStatus(`Not applied yet. In use: ${describe(filter)}.`, "warn");
  } else {
    setStatus(`In use: ${describe(filter)}.`, filter.source === "dashboard" ? "good" : "");
  }
}

async function send(body, expect) {
  sending = true;
  $("pf-apply").disabled = true;
  $("pf-defaults").disabled = true;
  setStatus("Sending…", "warn");
  try {
    const result = await sendPresenceFilter(deviceId, body);
    pending = { request_id: result.request_id, sentAt: Date.now(), expect };
    setStatus(`Sent (request ${result.request_id}), waiting for the controller…`, "warn");
  } catch (error) {
    pending = null;
    $("pf-apply").disabled = false;
    $("pf-defaults").disabled = false;
    setStatus(`Not sent: ${error.message}`, "bad");
  } finally {
    sending = false;
  }
}

for (const [id] of FIELDS) $(id).addEventListener("input", () => (edited = true));

$("pf-apply").addEventListener("click", () => {
  const body = {},
    expect = {};
  for (const [id, wire, key] of FIELDS) {
    const value = Number($(id).value);
    if ($(id).value === "" || !Number.isFinite(value) || value < Number($(id).min) || value > Number($(id).max)) {
      setStatus(`${$(id).parentElement.firstChild.textContent.trim()} must be ${$(id).min}–${$(id).max} s.`, "bad");
      return;
    }
    body[wire] = Math.round(value * 1000);
    expect[key] = body[wire] / 1000;
  }
  send(body, expect);
});

$("pf-defaults").addEventListener("click", () => {
  edited = false;
  send({ defaults: true }, "menuconfig");
});
