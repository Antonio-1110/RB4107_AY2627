"use strict";
// Away after / back after (Person card) and the buzzer / shut-off times (Timer card) in Tuning > Safety
// (docs/remote_reset.md): one "presence_filter" command to the controller, carrying all of them. Each card
// saves its own: its fields as edited, the other card's as they are in force. The gap a return may have is
// fixed on the dashboard, so the value in force is sent back unchanged. The controller's answer is
// presence_filter (and warning_after_seconds / shutdown_after_seconds) in the next telemetry.

import { sendPresenceFilter } from "./api.js";
import { $ } from "./dom.js";
import { number } from "./format.js";
import { registerPart, report, showsResult } from "./safety_save.js";

const ANSWER_WAIT_MS = 15000;
// [input id, key sent, key in the seconds form, card]
const FIELDS = [
  ["pf-absence", "absence_ms", "absence_seconds", "person"],
  ["pf-return", "return_ms", "return_seconds", "person"],
  ["pf-warning", "warning_ms", "warning_seconds", "timer"],
  ["pf-shutdown", "shutdown_ms", "shutdown_seconds", "timer"],
];
const LABEL = { person: "Away and back", timer: "Buzzer and shut-off times" };

let deviceId = "",
  edited = { person: false, timer: false }, // the user changed a card's inputs: don't overwrite them with telemetry
  sending = false,
  usable = false,
  inForce = null, // the filter in force (seconds)
  pending = null, // {request_id, sentAt, cards, expect: {key: seconds} or "menuconfig"}
  shutdownCountsFrom = null; // "UNATTENDED" or "WARNING", from telemetry

const setStatus = (cards, text, style = "") => {
  for (const card of cards) report(LABEL[card], text, style);
};

// The presence filter plus the warning/shutdown times, which telemetry reports separately.
function current(values) {
  const filter = values.presence_filter;
  if (!filter) return null;
  return {
    ...filter,
    warning_seconds: values.warning_after_seconds,
    shutdown_seconds: values.shutdown_after_seconds,
  };
}

function answered(filter) {
  if (!filter) return false;
  if (pending.expect === "menuconfig") return filter.source === "menuconfig";
  return filter.source === "dashboard" && FIELDS.every(([, , key]) => filter[key] === pending.expect[key]);
}

const DONE = {
  person: (f) => `saved: away after ${f.absence_seconds} s, back after ${f.return_seconds} s.`,
  timer: (f) => `saved: buzzer after ${f.warning_seconds} s, shut off after ${f.shutdown_seconds} s.`,
};

export function renderPresenceFilter(device) {
  if (device.device_id !== deviceId) {
    deviceId = device.device_id;
    edited = { person: false, timer: false };
    pending = null;
    setStatus(["person", "timer"], "");
  }
  shutdownCountsFrom = device.values.shutdown_counts_from;
  const filter = current(device.values),
    reachable = device.connection !== "offline";
  inForce = filter;
  if (filter) {
    for (const [id, , key, card] of FIELDS) if (!edited[card]) $(id).value = number(filter[key]) ? filter[key] : "";
  }

  if (pending) {
    if (answered(filter)) {
      for (const card of pending.cards) {
        edited[card] = false;
        report(LABEL[card], pending.expect === "menuconfig" ? "back to the menuconfig values." : DONE[card](filter), "good");
      }
      pending = null;
    } else if (Date.now() - pending.sentAt > ANSWER_WAIT_MS) {
      setStatus(
        pending.cards,
        'no change from the controller. Check it is online and that "Accept presence filter and buzzer/shutdown ' +
          'times from the dashboard" is on in menuconfig.',
        "bad",
      );
      pending = null;
    }
  } else if (!sending) {
    for (const card of ["person", "timer"]) if (!showsResult(LABEL[card])) report(LABEL[card], "");
  }
  usable = Boolean(filter) && reachable && pending === null && !sending;
  for (const [id] of FIELDS) $(id).disabled = !usable;
}

async function send(body, expect, cards) {
  sending = true;
  setStatus(cards, "sending…", "warn");
  try {
    const result = await sendPresenceFilter(deviceId, body);
    pending = { request_id: result.request_id, sentAt: Date.now(), cards, expect };
    setStatus(cards, `sent (request ${result.request_id}), waiting for the controller…`, "warn");
  } catch (error) {
    pending = null;
    setStatus(cards, `not sent: ${error.message}`, "bad");
  } finally {
    sending = false;
  }
}

for (const [id, , , card] of FIELDS)
  $(id).addEventListener("input", () => {
    edited[card] = true;
    report(LABEL[card], "");
  });

// saving: the cards being saved. Their fields go as edited, the rest as in force.
function prepare(saving) {
  const cards = ["person", "timer"].filter((card) => saving.has(card) && edited[card]),
    body = { return_gap_ms: Math.round((inForce.return_gap_seconds || 0) * 1000) },
    expect = {};
  for (const [id, wire, key, card] of FIELDS) {
    let value = inForce[key];
    if (cards.includes(card)) {
      const input = $(id);
      value = Number(input.value);
      if (input.value === "" || !Number.isFinite(value) || value < Number(input.min) || value > Number(input.max))
        return { error: `${input.parentElement.firstChild.textContent.trim()} must be ${input.min}–${input.max} s.` };
    }
    body[wire] = Math.round(value * 1000);
    expect[key] = body[wire] / 1000;
  }
  if (shutdownCountsFrom === "UNATTENDED" && body.shutdown_ms <= body.warning_ms)
    return { error: "shut off after must be longer than buzzer after (both count from when the person left)." };
  return { run: () => send(body, expect, cards) };
}

function defaults() {
  edited = { person: false, timer: false };
  send({ defaults: true }, "menuconfig", ["person", "timer"]);
}

for (const card of ["person", "timer"])
  registerPart({
    label: LABEL[card],
    card,
    shared: "presence_filter",
    edited: () => edited[card],
    busy: () => sending || pending !== null,
    usable: () => usable,
    prepare,
    defaults,
  });
