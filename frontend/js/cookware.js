"use strict";
// Cookware profile in the Cooking card (Tuning > Safety): how fast the pans at this stall lose heat. The
// controller judges from the temperature trend whether heat is going in, using the profile's cooling time
// constant (docs/safety_state_machine.md). It saves the profile and answers with cookware_profile /
// cookware_source in its telemetry. Saved with its card's Apply (safety_save.js).

import { sendCookware } from "./api.js";
import { $ } from "./dom.js";
import { registerPart, report, showsResult } from "./safety_save.js";

const ANSWER_WAIT_MS = 15000;
const LABEL = "Cookware profile";
const NAME = {
  mixed: "mixed or unknown",
  light: "light pans",
  heavy: "heavy pans",
  liquid: "pots with liquid",
};

let deviceId = "",
  edited = false,
  sending = false,
  usable = false,
  pending = null; // {request_id, sentAt, expect: profile name, or "menuconfig"}

const setStatus = (text, style = "") => report(LABEL, text, style);

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
    setStatus("");
  }
  const values = device.values || {},
    reported = Boolean(values.cookware_profile),
    reachable = device.connection !== "offline";
  if (reported && !edited) $("cw-select").value = values.cookware_profile;

  if (pending) {
    if (answered(values)) {
      pending = null;
      edited = false;
      setStatus(`saved, ${NAME[values.cookware_profile] || values.cookware_profile}.`, "good");
    } else if (Date.now() - pending.sentAt > ANSWER_WAIT_MS) {
      pending = null;
      setStatus(
        "the controller did not confirm the change. Check that its firmware supports this setting and that " +
          '"Accept the cookware profile from the dashboard" is enabled in menuconfig.',
        "bad",
      );
    }
  } else if (!sending && !showsResult(LABEL)) {
    setStatus("");
  }
  usable = reported && reachable && pending === null && !sending;
  $("cw-select").disabled = !usable;
}

async function send(body, expect) {
  sending = true;
  setStatus("sending…", "warn");
  try {
    const result = await sendCookware(deviceId, body);
    pending = { request_id: result.request_id, sentAt: Date.now(), expect };
    setStatus(`sent (request ${result.request_id}); waiting for the controller to confirm…`, "warn");
  } catch (error) {
    pending = null;
    setStatus(`not sent: ${error.message}`, "bad");
  } finally {
    sending = false;
  }
}

$("cw-select").addEventListener("input", () => {
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
    const profile = $("cw-select").value;
    return { run: () => send({ cookware: profile }, profile) };
  },
  defaults() {
    edited = false;
    send({ defaults: true }, "menuconfig");
  },
});
