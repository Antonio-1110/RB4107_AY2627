"use strict";
// Temperature tiers 2 to 5 in the Timer rule (Tuning > Rules): the hotter the pan, the shorter the buzzer
// and shutdown times; tier 1 is the base times (docs/safety_state_machine.md). The controller checks the
// values, saves them and answers with tiers / tiers_source in its telemetry.

import { sendTiers } from "./api.js";
import { $ } from "./dom.js";
import { number } from "./format.js";

const ANSWER_WAIT_MS = 15000;
const TIERS = [2, 3, 4, 5];
// [input id, tier, key in the telemetry's seconds form, key sent]
const FIELDS = TIERS.flatMap((n) => [
  [`tr-${n}-temp`, n, "temp_c", "temp_c"],
  [`tr-${n}-warn`, n, "warning_seconds", "warning_s"],
  [`tr-${n}-stop`, n, "shutdown_seconds", "shutdown_s"],
]);
const level = (tiers, n) => ((tiers && tiers.levels) || []).find((t) => t && t.tier === n) || {};

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
  TIERS.map((n) => level(t, n))
    .filter((l) => number(l.temp_c) && l.temp_c > 0)
    .map((l) => `tier ${l.tier} from ${l.temp_c} °C: ${l.warning_seconds}/${l.shutdown_seconds} s`)
    .join("; ") + ` (${source === "dashboard" ? "set from the dashboard" : "menuconfig"})`;

function answered(values) {
  if (pending.expect === "menuconfig") return values.tiers_source === "menuconfig";
  if (values.tiers_source !== "dashboard" || !values.tiers) return false;
  return FIELDS.every(([id, n, key]) => Math.abs(level(values.tiers, n)[key] - pending.expect[id]) < 0.06);
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
    tiers = values.tiers && Array.isArray(values.tiers.levels) ? values.tiers : null,
    reachable = device.connection !== "offline";
  countsFrom = values.shutdown_counts_from;
  if (tiers && !edited) {
    for (const [id, n, key] of FIELDS) {
      const v = level(tiers, n)[key];
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
        "The controller didn't report the change. It refuses inconsistent values (a shutdown not after its warning, a tier below the one before it) " +
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
  const levels = TIERS.map(() => ({})),
    expect = {};
  for (const [id, n, , sentKey] of FIELDS) {
    const input = $(id),
      value = Number(input.value);
    if (input.value === "" || !Number.isFinite(value) || value < Number(input.min) || value > Number(input.max)) {
      setStatus(`${input.parentElement.firstChild.textContent.trim()} (tier ${n}) must be ${input.min}–${input.max}.`, "bad");
      return;
    }
    levels[TIERS.indexOf(n)][sentKey] = value;
    expect[id] = value;
  }
  let below = 0;
  for (const [k, l] of levels.entries()) {
    if (l.temp_c > 0) {
      if (l.temp_c < below) {
        setStatus(`Tier ${TIERS[k]} must not start below the tier before it.`, "bad");
        return;
      }
      below = l.temp_c;
    }
    if (countsFrom === "UNATTENDED" && l.shutdown_s <= l.warning_s) {
      setStatus(`In tier ${TIERS[k]} the shutdown must come after the buzzer (both count from when the person left).`, "bad");
      return;
    }
  }
  send({ levels }, expect);
});

$("tr-defaults").addEventListener("click", () => {
  edited = false;
  send({ defaults: true }, "menuconfig");
});
