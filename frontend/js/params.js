"use strict";
// The few safety settings with no main field of their own, under the Cooking and Timer cards' More settings
// (Tuning > Safety), from the controller's table firmware/components/rb_params/include/rb_params.def (copied
// into safety_rules_data.js by tools/safety_rules.py gen; each group goes to the card of the same name). One
// "params" command carries all of them: each card saves its own as edited and the other card's as in force.
// The controller checks them, saves them and answers with params / params_source in its telemetry. Values
// travel in the table's units (ms, 0.1 °C, ...).

import RULES from "./safety_rules_data.js";
import { sendParams } from "./api.js";
import { $, make } from "./dom.js";
import { registerPart, report, showsResult } from "./safety_save.js";

const ANSWER_WAIT_MS = 15000;
const cardOf = (p) => (p.group || "").toLowerCase(); // "cooking" or "timer"
const LABEL = { cooking: "Cooking details", timer: "Timer details" };
// Wire unit -> [shown = wire / scale, unit, input step]
const KIND = {
  MS: [1000, "s", 1],
  DC: [10, "°C", 0.5],
  DC_PER_MIN: [10, "°C/min", 0.1],
  TENTHS: [10, "", 0.1],
  COUNT: [1, "", 1],
};

export const PARAMS = RULES.params;

// A value in the table's units as the dashboard writes it ("90 s", "on").
export function paramText(p, v) {
  if (p.options) return p.options[v] ?? String(v);
  const [scale, unit] = KIND[p.kind];
  return `${Math.round((v / scale) * 10) / 10}${unit ? ` ${unit}` : ""}`;
}

let deviceId = "",
  edited = { cooking: false, timer: false },
  reported = null, // the values in force
  sending = false,
  usable = false,
  pending = null; // {request_id, sentAt, cards, expect: {key: value} or "menuconfig"}

const inputId = (p) => `pr-${p.key}`;
const setStatus = (cards, text, style = "") => {
  for (const card of cards) report(LABEL[card], text, style);
};

// One labelled input or select per setting, in its card's More settings.
function build() {
  for (const p of PARAMS) {
    const host = $(`pr-fields-${cardOf(p)}`);
    if (!host) throw new Error(`rb_params.def group "${p.group}" has no pr-fields-… container on the Safety tab`);
    const label = make("label", p.options || !KIND[p.kind][1] ? p.label : `${p.label} (${KIND[p.kind][1]})`);
    let input;
    if (p.options) {
      input = make("select");
      p.options.forEach((text, i) => {
        const option = make("option", text);
        option.value = String(i);
        input.append(option);
      });
    } else {
      const [scale, , step] = KIND[p.kind];
      input = make("input");
      input.type = "number";
      input.min = String(p.min / scale);
      input.max = String(p.max / scale);
      input.step = String(step);
    }
    input.id = inputId(p);
    input.disabled = true;
    input.title = `Menuconfig default: ${paramText(p, p.default)}`;
    input.addEventListener("input", () => {
      edited[cardOf(p)] = true;
      report(LABEL[cardOf(p)], "");
    });
    label.append(input);
    host.append(label);
  }
}

function show(p, v) {
  const input = $(inputId(p));
  input.value = p.options ? String(v) : String(Math.round((v / KIND[p.kind][0]) * 1000) / 1000);
}

// The value an input holds, in the table's units, or null if it is out of range.
function read(p) {
  const input = $(inputId(p));
  if (input.value === "") return null;
  const v = p.options ? Number(input.value) : Math.round(Number(input.value) * KIND[p.kind][0]);
  return Number.isInteger(v) && v >= p.min && v <= p.max ? v : null;
}

function answered(values) {
  if (pending.expect === "menuconfig") return values.params_source === "menuconfig";
  return (
    values.params_source === "dashboard" &&
    values.params &&
    PARAMS.every((p) => values.params[p.key] === pending.expect[p.key])
  );
}

export function renderParams(device) {
  if (!device) return;
  if (device.device_id !== deviceId) {
    deviceId = device.device_id;
    edited = { cooking: false, timer: false };
    pending = null;
    setStatus(["cooking", "timer"], "");
  }
  const values = device.values || {},
    reachable = device.connection !== "offline";
  reported = values.params || null;
  if (reported)
    for (const p of PARAMS) if (!edited[cardOf(p)] && reported[p.key] !== undefined) show(p, reported[p.key]);
  if (pending) {
    if (answered(values)) {
      for (const card of pending.cards) edited[card] = false;
      setStatus(pending.cards, pending.expect === "menuconfig" ? "back to the menuconfig values." : "saved.", "good");
      pending = null;
    } else if (Date.now() - pending.sentAt > ANSWER_WAIT_MS) {
      setStatus(
        pending.cards,
        'the controller didn\'t report the change. It needs "Accept the other safety settings from the dashboard" ' +
          "on in menuconfig, and firmware with the same settings as this dashboard.",
        "bad",
      );
      pending = null;
    }
  } else if (!sending) {
    for (const card of ["cooking", "timer"]) if (!showsResult(LABEL[card])) report(LABEL[card], "");
  }
  usable = Boolean(reported) && reachable && pending === null && !sending;
  for (const p of PARAMS) $(inputId(p)).disabled = !usable;
}

async function send(body, expect, cards) {
  sending = true;
  setStatus(cards, "sending…", "warn");
  try {
    const result = await sendParams(deviceId, body);
    pending = { request_id: result.request_id, sentAt: Date.now(), cards, expect };
    setStatus(cards, `sent (request ${result.request_id}), waiting for the controller…`, "warn");
  } catch (error) {
    pending = null;
    setStatus(cards, `not sent: ${error.message}`, "bad");
  } finally {
    sending = false;
  }
}

build();

// saving: the cards being saved. Their settings go as edited, the rest as in force.
function prepare(saving) {
  const cards = ["cooking", "timer"].filter((card) => saving.has(card) && edited[card]),
    body = {};
  for (const p of PARAMS) {
    if (!cards.includes(cardOf(p))) {
      body[p.key] = reported[p.key];
      continue;
    }
    const v = read(p);
    if (v === null) return { error: `"${p.label}" must be ${paramText(p, p.min)} to ${paramText(p, p.max)}.` };
    body[p.key] = v;
  }
  return { run: () => send(body, body, cards) };
}

function defaults() {
  edited = { cooking: false, timer: false };
  send({ defaults: true }, "menuconfig", ["cooking", "timer"]);
}

for (const card of ["cooking", "timer"])
  registerPart({
    label: LABEL[card],
    card,
    shared: "params",
    edited: () => edited[card],
    busy: () => sending || pending !== null,
    usable: () => usable,
    prepare,
    defaults,
  });
