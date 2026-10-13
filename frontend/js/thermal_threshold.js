"use strict";
// Hot-pixel threshold in the Thermal camera card (docs/remote_reset.md). The
// controller forwards it to the thermal node, which uses it from the next frame
// and saves it. The node sends no separate answer: the threshold in use comes
// back in every heat-map picture (hot_threshold_c). Display and telemetry only;
// the safety logic does not use it.

import { sendThermalThreshold } from "./api.js";
import { $ } from "./dom.js";

const ANSWER_WAIT_MS = 15000; // a picture every 3 s by default
const PICTURES_AFTER_DEFAULTS = 2; // one picture may already be on its way when the command lands

let deviceId = "",
  edited = false, // the user changed the input: don't overwrite it with the picture's value
  sending = false,
  pending = null, // {request_id, sentAt, expect: degC, or "menuconfig" with startNumber}
  latestNumber = null; // number of the newest picture seen

function setStatus(text, style = "") {
  const status = $("tt-status");
  status.textContent = text;
  status.className = `tuning-status ${style || "muted"}`;
}

function answered(threshold, number) {
  if (pending.expect === "menuconfig") {
    return number !== null && pending.startNumber !== null && number - pending.startNumber >= PICTURES_AFTER_DEFAULTS;
  }
  return threshold !== null && Math.abs(threshold - pending.expect) < 0.05;
}

// device: the selected controller (or null); picture: decoded heat map (or null);
// number: that picture's number.
export function renderThermalThreshold(device, picture, number) {
  if (!device) return;
  latestNumber = number;
  if (device.device_id !== deviceId) {
    deviceId = device.device_id;
    edited = false;
    pending = null;
    setStatus("Waiting for the controller.");
  }
  const threshold = picture ? picture.threshold : null,
    reachable = device.connection !== "offline",
    inUse = threshold !== null ? `${threshold.toFixed(1)} °C` : "not known (no heat-map picture yet)";
  if (threshold !== null && !edited) $("tt-value").value = threshold.toFixed(1);

  if (pending) {
    if (answered(threshold, number)) {
      const back = pending.expect === "menuconfig";
      pending = null;
      edited = false;
      if (threshold !== null) $("tt-value").value = threshold.toFixed(1);
      setStatus(`${back ? "Back to the menuconfig value" : "Applied and saved on the node"}: ${inUse}.`, "good");
    } else if (Date.now() - pending.sentAt > ANSWER_WAIT_MS) {
      pending = null;
      setStatus(
        "No change seen in the heat-map pictures. Check that the controller and thermal node run firmware " +
          "with this feature, that both menuconfig options for the dashboard threshold are on, and that the " +
          "heat map is on.",
        "bad",
      );
    } else {
      setStatus(`Sent (request ${pending.request_id}), waiting for the next picture…`, "warn");
    }
  } else if (!reachable) {
    setStatus(`Controller offline. Last known: ${inUse}.`, "warn");
  } else if (sending || ["bad", "good"].some((style) => $("tt-status").classList.contains(style))) {
    // keep "sending…", the last error or the last result until the next click or edit
  } else if (edited) {
    setStatus(`Not applied yet. In use: ${inUse}.`, "warn");
  } else {
    setStatus(`In use: ${inUse}.`);
  }
  const usable = reachable && pending === null && !sending;
  $("tt-value").disabled = !usable;
  $("tt-apply").disabled = !usable;
  $("tt-defaults").disabled = !usable;
}

async function send(body, expect, startNumber = null) {
  sending = true;
  $("tt-apply").disabled = true;
  $("tt-defaults").disabled = true;
  setStatus("Sending…", "warn");
  try {
    const result = await sendThermalThreshold(deviceId, body);
    pending = { request_id: result.request_id, sentAt: Date.now(), expect, startNumber };
    setStatus(`Sent (request ${result.request_id}), waiting for the next picture…`, "warn");
  } catch (error) {
    pending = null;
    $("tt-apply").disabled = false;
    $("tt-defaults").disabled = false;
    setStatus(`Not sent: ${error.message}`, "bad");
  } finally {
    sending = false;
  }
}

$("tt-value").addEventListener("input", () => {
  edited = true;
  setStatus("Not applied yet.", "warn");
});

$("tt-apply").addEventListener("click", () => {
  const input = $("tt-value"),
    value = Number(input.value);
  if (input.value === "" || !Number.isFinite(value) || value < Number(input.min) || value > Number(input.max)) {
    setStatus(`The threshold must be ${input.min}–${input.max} °C.`, "bad");
    return;
  }
  const rounded = Math.round(value * 10) / 10; // the node works in 0.1 °C steps
  send({ hot_pixel_threshold_c: rounded }, rounded);
});

$("tt-defaults").addEventListener("click", () => {
  edited = false;
  send({ defaults: true }, "menuconfig", latestNumber);
});
