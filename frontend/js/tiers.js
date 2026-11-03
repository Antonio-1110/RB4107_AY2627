"use strict";
// Temperature tiers 2 to 5 under the Timer card's More settings (Tuning > Safety): the hotter the pan, the
// shorter the buzzer and shut-off times; tier 1 is the base times (docs/safety_state_machine.md). The
// controller checks the values, saves them and answers with tiers / tiers_source in its telemetry. Saved with
// its card's Apply (safety_save.js).

import { sendTiers } from "./api.js";
import { $ } from "./dom.js";
import { number } from "./format.js";
import { registerPart, report, showsResult } from "./safety_save.js";

const ANSWER_WAIT_MS = 15000;
const LABEL = "Temperature tiers";
const TIERS = [2, 3, 4, 5];
// [input id, tier, key in the telemetry's seconds form, key sent, what it is]
const FIELDS = TIERS.flatMap((n) => [
  [`tr-${n}-temp`, n, "temp_c", "temp_c", "from (°C)"],
  [`tr-${n}-warn`, n, "warning_seconds", "warning_s", "buzzer warning (s)"],
  [`tr-${n}-stop`, n, "shutdown_seconds", "shutdown_s", "shut-off (s)"],
]);
const level = (tiers, n) => ((tiers && tiers.levels) || []).find((t) => t && t.tier === n) || {};

let deviceId = "",
  edited = false,
  sending = false,
  usable = false,
  pending = null, // {request_id, sentAt, expect: {id: value} or "menuconfig"}
  countsFrom = null; // "UNATTENDED" or "WARNING"

const setStatus = (text, style = "") => report(LABEL, text, style);

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
    setStatus("");
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
  if (pending) {
    if (answered(values)) {
      pending = null;
      edited = false;
      setStatus("saved.", "good");
    } else if (Date.now() - pending.sentAt > ANSWER_WAIT_MS) {
      pending = null;
      setStatus(
        "the controller did not confirm the change. It rejects inconsistent values (a shut-off not later than its " +
          'buzzer warning, or a tier starting below the previous one) and requires "Accept the temperature tiers from ' +
          'the dashboard" to be enabled in menuconfig.',
        "bad",
      );
    }
  } else if (!sending && !showsResult(LABEL)) {
    setStatus("");
  }
  usable = Boolean(tiers) && reachable && pending === null && !sending;
  for (const [id] of FIELDS) $(id).disabled = !usable;
}

async function send(body, expect) {
  sending = true;
  setStatus("sending…", "warn");
  try {
    const result = await sendTiers(deviceId, body);
    pending = { request_id: result.request_id, sentAt: Date.now(), expect };
    setStatus(`sent (request ${result.request_id}); waiting for the controller to confirm…`, "warn");
  } catch (error) {
    pending = null;
    setStatus(`not sent: ${error.message}`, "bad");
  } finally {
    sending = false;
  }
}

for (const [id] of FIELDS)
  $(id).addEventListener("input", () => {
    edited = true;
    setStatus("");
  });

registerPart({
  label: LABEL,
  card: "timer",
  edited: () => edited,
  busy: () => sending || pending !== null,
  usable: () => usable,
  prepare() {
    const levels = TIERS.map(() => ({})),
      expect = {};
    for (const [id, n, , sentKey, what] of FIELDS) {
      const input = $(id),
        value = Number(input.value);
      if (input.value === "" || !Number.isFinite(value) || value < Number(input.min) || value > Number(input.max))
        return { error: `tier ${n} ${what} must be ${input.min}–${input.max}.` };
      levels[TIERS.indexOf(n)][sentKey] = value;
      expect[id] = value;
    }
    let below = 0;
    for (const [k, l] of levels.entries()) {
      if (l.temp_c > 0) {
        if (l.temp_c < below) return { error: `tier ${TIERS[k]} must not start below the previous tier.` };
        below = l.temp_c;
      }
      if (countsFrom === "UNATTENDED" && l.shutdown_s <= l.warning_s)
        return { error: `in tier ${TIERS[k]} the gas shut-off must come after the buzzer warning.` };
    }
    return { run: () => send({ levels }, expect) };
  },
  defaults() {
    edited = false;
    send({ defaults: true }, "menuconfig");
  },
});
