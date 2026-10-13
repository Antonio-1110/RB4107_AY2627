"use strict";
// Cookware profile in the Cooking rule (Tuning > Rules): how fast the pans at this stall lose
// heat. The controller judges from the temperature trend whether heat is going in, using the
// profile's cooling time constant (docs/safety_state_machine.md). It saves the profile and
// answers with cookware_profile / cookware_source in its telemetry.

import { sendCookware } from "./api.js";
import { $ } from "./dom.js";

const ANSWER_WAIT_MS = 15000;
const LABEL = {
  mixed: "mixed / unknown",
  light: "light pans",
  heavy: "heavy pans",
  liquid: "pots with liquid",
};

let deviceId = "",
  edited = false,
  sending = false,
  pending = null; // {request_id, sentAt, expect: profile name, or "menuconfig"}

function setStatus(text, style = "") {
  const status = $("cw-status");
  status.textContent = text;
  status.className = `tuning-status ${style || "muted"}`;
}

const inUseText = (values) =>
  values.cookware_profile
    ? `${LABEL[values.cookware_profile] || values.cookware_profile}` +
      `${values.cookware_tau_s ? `, time constant ${Math.round(values.cookware_tau_s / 60)} min` : ""}` +
      ` (${values.cookware_source === "dashboard" ? "set from the dashboard" : "menuconfig"})`
    : "not reported (controller firmware without this feature?)";

function answered(values) {
  if (pending.expect === "menuconfig") return values.cookware_source === "menuconfig";
  return values.cookware_profile === pending.expect && values.cookware_source === "dashboard";
}

export function renderCookware(device) {
  if (!device) return;
  if (device.device_id !== deviceId) {
    deviceId = device.device_id;
    edited = false;
    pending = null;
    setStatus("Waiting for the controller.");
  }
  const values = device.values || {},
    reported = Boolean(values.cookware_profile),
    reachable = device.connection !== "offline",
    inUse = inUseText(values);
  if (reported && !edited) $("cw-select").value = values.cookware_profile;

  if (pending) {
    if (answered(values)) {
      pending = null;
      edited = false;
      setStatus(`Applied: ${inUse}.`, "good");
    } else if (Date.now() - pending.sentAt > ANSWER_WAIT_MS) {
      pending = null;
      setStatus(
        "The controller didn't report the change. Check that it runs firmware with this feature and that " +
          '"Accept the cookware profile from the dashboard" is on in menuconfig.',
        "bad",
      );
    } else {
      setStatus(`Sent (request ${pending.request_id}), waiting for the controller…`, "warn");
    }
  } else if (!reachable) {
    setStatus(`Controller offline. Last known: ${inUse}.`, "warn");
  } else if (sending || ["bad", "good"].some((style) => $("cw-status").classList.contains(style))) {
    // keep "sending…", the last error or the last result until the next click or edit
  } else if (edited) {
    setStatus(`Not applied yet. In use: ${inUse}.`, "warn");
  } else {
    setStatus(`In use: ${inUse}.`);
  }
  const usable = reported && reachable && pending === null && !sending;
  $("cw-select").disabled = !usable;
  $("cw-apply").disabled = !usable;
  $("cw-defaults").disabled = !usable;
}

async function send(body, expect) {
  sending = true;
  $("cw-apply").disabled = true;
  $("cw-defaults").disabled = true;
  setStatus("Sending…", "warn");
  try {
    const result = await sendCookware(deviceId, body);
    pending = { request_id: result.request_id, sentAt: Date.now(), expect };
    setStatus(`Sent (request ${result.request_id}), waiting for the controller…`, "warn");
  } catch (error) {
    pending = null;
    $("cw-apply").disabled = false;
    $("cw-defaults").disabled = false;
    setStatus(`Not sent: ${error.message}`, "bad");
  } finally {
    sending = false;
  }
}

$("cw-select").addEventListener("input", () => {
  edited = true;
  setStatus("Not applied yet.", "warn");
});
$("cw-apply").addEventListener("click", () => send({ cookware: $("cw-select").value }, $("cw-select").value));
$("cw-defaults").addEventListener("click", () => {
  edited = false;
  send({ defaults: true }, "menuconfig");
});
