"use strict";
// "Reset shutdown" button in the System status card (docs/remote_reset.md).
// Sends the operator reset; the controller's answer is reset_required going
// false (and the state leaving SHUTDOWN) in the next telemetry.

import { sendReset } from "./api.js";
import { $ } from "./dom.js";

const ANSWER_WAIT_MS = 15000;

let deviceId = "",
  pending = null; // {request_id, sentAt}

function setStatus(text, style = "") {
  const status = $("reset-status");
  status.textContent = text;
  status.className = `tuning-status ${style || "muted"}`;
}

export function renderReset(device) {
  if (device.device_id !== deviceId) {
    deviceId = device.device_id;
    pending = null;
    setStatus("Available only while a shutdown is latched.");
  }
  const required = device.values.manual_reset_required === true,
    reachable = device.connection !== "offline";
  $("reset-button").disabled = !required || !reachable || pending !== null;

  if (pending) {
    if (!required) {
      pending = null;
      setStatus("Reset done: the controller left SHUTDOWN.", "good");
    } else if (Date.now() - pending.sentAt > ANSWER_WAIT_MS) {
      pending = null;
      setStatus(
        "No change from the controller. Check it is online and that \"Accept the operator reset from the dashboard\" is on in menuconfig.",
        "bad",
      );
    } else {
      setStatus(`Reset sent (request ${pending.request_id}), waiting for the controller…`, "warn");
    }
  } else if (required && !reachable) {
    setStatus("Shutdown latched, but the controller is offline: reset it at the device.", "warn");
  } else if (required && device.connection !== "online") {
    setStatus("Shutdown latched (last known: telemetry is not current, so the result may not show).", "warn");
  } else if (required) {
    setStatus("Shutdown latched. Check the stove in person, then reset here.", "warn");
  } else if (!$("reset-status").classList.contains("good")) {
    setStatus("Available only while a shutdown is latched.");
  }
}

$("reset-button").addEventListener("click", async () => {
  if (
    !confirm(
      `Release the shutdown on ${deviceId}? The cooker supply comes back on. ` +
        "Only do this after someone has checked the stove in person.",
    )
  )
    return;
  $("reset-button").disabled = true;
  try {
    const result = await sendReset(deviceId);
    pending = { request_id: result.request_id, sentAt: Date.now() };
    setStatus(`Reset sent (request ${result.request_id}), waiting for the controller…`, "warn");
  } catch (error) {
    pending = null;
    $("reset-button").disabled = false;
    setStatus(`Reset not sent: ${error.message}`, "bad");
  }
});
