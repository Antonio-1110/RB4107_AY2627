"use strict";
// "Reset shut-off" button in the countdown card (docs/remote_reset.md).
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
    setStatus("Available only while a shut-off is latched.");
  }
  const required = device.values.manual_reset_required === true,
    reachable = device.connection !== "offline";
  $("reset-button").disabled = !required || !reachable || pending !== null;

  if (pending) {
    if (!required) {
      pending = null;
      setStatus("Reset confirmed: the controller has left SHUTDOWN.", "good");
    } else if (Date.now() - pending.sentAt > ANSWER_WAIT_MS) {
      pending = null;
      setStatus(
        "The controller did not confirm the reset. Check that it is online and that \"Accept the operator reset from the dashboard\" is enabled in menuconfig.",
        "bad",
      );
    } else {
      setStatus(`Reset sent (request ${pending.request_id}); waiting for the controller to confirm…`, "warn");
    }
  } else if (required && !reachable) {
    setStatus("Shut-off latched, but the controller is offline: reset it at the controller.", "warn");
  } else if (required && device.connection !== "online") {
    setStatus("Shut-off latched (last known: telemetry is out of date, so the result may not appear).", "warn");
  } else if (required) {
    setStatus("Shut-off latched. Inspect the stove in person, then reset here.", "warn");
  } else if (!$("reset-status").classList.contains("good")) {
    setStatus("Available only while a shut-off is latched.");
  }
}

$("reset-button").addEventListener("click", async () => {
  if (
    !confirm(
      `Reset the shut-off on ${deviceId}? The gas supply to the stove is restored. ` +
        "Proceed only after the stove has been inspected in person.",
    )
  )
    return;
  $("reset-button").disabled = true;
  try {
    const result = await sendReset(deviceId);
    pending = { request_id: result.request_id, sentAt: Date.now() };
    setStatus(`Reset sent (request ${result.request_id}); waiting for the controller to confirm…`, "warn");
  } catch (error) {
    pending = null;
    $("reset-button").disabled = false;
    setStatus(`Reset not sent: ${error.message}`, "bad");
  }
});
