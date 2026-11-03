"use strict";
// Cooking temperature in the Cooking card (Tuning > Safety; docs/remote_reset.md). The controller counts it
// as cooking when the hottest pixel reaches this value, saves it, and forwards it to the thermal node as its
// hot-pixel threshold so the outlined pixels are the ones that count. The controller's answer is
// cooking_on_c / cooking_source in its telemetry. Saved with its card's Apply (safety_save.js).

import { sendCookingTemp } from "./api.js";
import { $ } from "./dom.js";
import { registerPart, report, showsResult } from "./safety_save.js";

const ANSWER_WAIT_MS = 15000; // telemetry every few seconds
const LABEL = "Cooking temperature";

let deviceId = "",
  edited = false, // the user changed the input: don't overwrite it with the reported value
  sending = false,
  usable = false,
  pending = null; // {request_id, sentAt, expect: degC, or "menuconfig"}

const setStatus = (text, style = "") => report(LABEL, text, style);

const reported = (values) => (typeof values.cooking_on_c === "number" ? values.cooking_on_c : null);

function answered(values) {
  const on = reported(values);
  if (pending.expect === "menuconfig") return values.cooking_source === "menuconfig";
  return on !== null && values.cooking_source === "dashboard" && Math.abs(on - pending.expect) < 0.05;
}

// device: the selected controller (or null).
export function renderCookingTemp(device) {
  if (!device) return;
  if (device.device_id !== deviceId) {
    deviceId = device.device_id;
    edited = false;
    pending = null;
    setStatus("");
  }
  const values = device.values || {},
    on = reported(values),
    reachable = device.connection !== "offline";
  if (on !== null && !edited) $("ct-value").value = on.toFixed(1);

  if (pending) {
    if (answered(values)) {
      const back = pending.expect === "menuconfig";
      pending = null;
      edited = false;
      if (on !== null) $("ct-value").value = on.toFixed(1);
      setStatus(back ? `back to the menuconfig value, ${on.toFixed(1)} °C.` : `saved, ${on.toFixed(1)} °C.`, "good");
    } else if (Date.now() - pending.sentAt > ANSWER_WAIT_MS) {
      pending = null;
      setStatus(
        "the controller didn't report the change. Check that it runs firmware with this feature and that " +
          '"Accept the cooking temperature from the dashboard" is on in menuconfig.',
        "bad",
      );
    }
  } else if (!sending && !showsResult(LABEL)) {
    setStatus("");
  }
  usable = on !== null && reachable && pending === null && !sending;
  $("ct-value").disabled = !usable;
}

async function send(body, expect) {
  sending = true;
  setStatus("sending…", "warn");
  try {
    const result = await sendCookingTemp(deviceId, body);
    pending = { request_id: result.request_id, sentAt: Date.now(), expect };
    setStatus(`sent (request ${result.request_id}), waiting for the controller…`, "warn");
  } catch (error) {
    pending = null;
    setStatus(`not sent: ${error.message}`, "bad");
  } finally {
    sending = false;
  }
}

$("ct-value").addEventListener("input", () => {
  edited = true;
  setStatus("");
});

registerPart({
  label: LABEL,
  card: "cooking",
  edited: () => edited,
  busy: () => sending || pending !== null,
  usable: () => usable,
  prepare() {
    const input = $("ct-value"),
      value = Number(input.value);
    if (input.value === "" || !Number.isFinite(value) || value < Number(input.min) || value > Number(input.max))
      return { error: `must be ${input.min}–${input.max} °C.` };
    const rounded = Math.round(value * 10) / 10; // the controller works in 0.1 °C steps
    return { run: () => send({ cooking_temp_c: rounded }, rounded) };
  },
  defaults() {
    edited = false;
    send({ defaults: true }, "menuconfig");
  },
});
