"use strict";
// Cooking temperature in the Thermal camera card (docs/remote_reset.md). The
// controller counts it as cooking when the hottest pixel reaches this value,
// saves it, and forwards it to the thermal node as its hot-pixel threshold so
// the outlined pixels are the ones that count. The controller's answer is
// cooking_on_c / cooking_source in its telemetry.

import { sendCookingTemp } from "./api.js";
import { $ } from "./dom.js";

const ANSWER_WAIT_MS = 15000; // telemetry every few seconds

let deviceId = "",
  edited = false, // the user changed the input: don't overwrite it with the reported value
  sending = false,
  pending = null; // {request_id, sentAt, expect: degC, or "menuconfig"}

function setStatus(text, style = "") {
  const status = $("ct-status");
  status.textContent = text;
  status.className = `tuning-status ${style || "muted"}`;
}

const reported = (values) => (typeof values.cooking_on_c === "number" ? values.cooking_on_c : null);

function answered(values) {
  const on = reported(values);
  if (pending.expect === "menuconfig") return values.cooking_source === "menuconfig";
  return on !== null && values.cooking_source === "dashboard" && Math.abs(on - pending.expect) < 0.05;
}

function inUseText(values) {
  const on = reported(values),
    off = typeof values.cooking_off_c === "number" ? values.cooking_off_c : null;
  if (on === null) return "not reported (controller firmware without this feature?)";
  const source = values.cooking_source === "dashboard" ? "set from the dashboard" : "menuconfig";
  return `cooking from ${on.toFixed(1)} °C${off !== null ? `, over below ${off.toFixed(1)} °C` : ""} (${source})`;
}

// device: the selected controller (or null).
export function renderCookingTemp(device) {
  if (!device) return;
  if (device.device_id !== deviceId) {
    deviceId = device.device_id;
    edited = false;
    pending = null;
    setStatus("Waiting for the controller.");
  }
  const values = device.values || {},
    on = reported(values),
    reachable = device.connection !== "offline",
    inUse = inUseText(values);
  if (on !== null && !edited) $("ct-value").value = on.toFixed(1);

  if (pending) {
    if (answered(values)) {
      const back = pending.expect === "menuconfig";
      pending = null;
      edited = false;
      if (on !== null) $("ct-value").value = on.toFixed(1);
      setStatus(`${back ? "Back to the menuconfig value" : "Applied and saved on the controller"}: ${inUse}.`, "good");
    } else if (Date.now() - pending.sentAt > ANSWER_WAIT_MS) {
      pending = null;
      setStatus(
        "The controller didn't report the change. Check that it runs firmware with this feature and that " +
          '"Accept the cooking temperature from the dashboard" is on in menuconfig.',
        "bad",
      );
    } else {
      setStatus(`Sent (request ${pending.request_id}), waiting for the controller…`, "warn");
    }
  } else if (!reachable) {
    setStatus(`Controller offline. Last known: ${inUse}.`, "warn");
  } else if (sending || ["bad", "good"].some((style) => $("ct-status").classList.contains(style))) {
    // keep "sending…", the last error or the last result until the next click or edit
  } else if (edited) {
    setStatus(`Not applied yet. In use: ${inUse}.`, "warn");
  } else {
    setStatus(`In use: ${inUse}.`);
  }
  const usable = reachable && pending === null && !sending;
  $("ct-value").disabled = !usable;
  $("ct-apply").disabled = !usable;
  $("ct-defaults").disabled = !usable;
}

async function send(body, expect) {
  sending = true;
  $("ct-apply").disabled = true;
  $("ct-defaults").disabled = true;
  setStatus("Sending…", "warn");
  try {
    const result = await sendCookingTemp(deviceId, body);
    pending = { request_id: result.request_id, sentAt: Date.now(), expect };
    setStatus(`Sent (request ${result.request_id}), waiting for the controller…`, "warn");
  } catch (error) {
    pending = null;
    $("ct-apply").disabled = false;
    $("ct-defaults").disabled = false;
    setStatus(`Not sent: ${error.message}`, "bad");
  } finally {
    sending = false;
  }
}

$("ct-value").addEventListener("input", () => {
  edited = true;
  setStatus("Not applied yet.", "warn");
});

$("ct-apply").addEventListener("click", () => {
  const input = $("ct-value"),
    value = Number(input.value);
  if (input.value === "" || !Number.isFinite(value) || value < Number(input.min) || value > Number(input.max)) {
    setStatus(`The cooking temperature must be ${input.min}–${input.max} °C.`, "bad");
    return;
  }
  const rounded = Math.round(value * 10) / 10; // the controller works in 0.1 °C steps
  send({ cooking_temp_c: rounded }, rounded);
});

$("ct-defaults").addEventListener("click", () => {
  edited = false;
  send({ defaults: true }, "menuconfig");
});
