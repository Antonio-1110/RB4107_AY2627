"use strict";
// Applying settings on the Safety tab (Tuning > Safety): each card's own Apply, and Restore defaults at the
// bottom (there is no page-wide save: the user wants each section applied on its own). Each part (cooking temperature, cookware, presence and times, attendance, radar rules, tiers,
// the More settings) registers here with the card it is on: it says whether the user changed it, checks and
// sends its own command, and reports how that went. A card's Apply sends only that card's changed parts, once
// all of them check out.
//
// Two commands span two cards (presence_filter: away/back on Person, buzzer/shut-off on Timer; params: the
// More settings of Cooking and Timer). Their parts share a `shared` key: prepare() then gets the cards being
// saved, sends those cards' fields as edited and the other card's as they are in force, and the coordinator
// calls it once for the group.

import { $, make } from "./dom.js";

// {label, card, shared?, edited(), busy(), usable(), prepare(cards: Set) -> {error} | {run}, defaults()}
const parts = [];
const status = new Map(); // label -> {text, style}: "warn" waiting, "good" answered, "bad" not sent or no answer
const CARDS = ["cooking", "person", "timer"];

export function registerPart(part) {
  parts.push(part);
}

// A part's latest result; an empty text clears it.
export function report(label, text, style = "") {
  status.set(label, { text, style });
  refresh();
}

// The part shows a result (saved, or an error) that stays until the next edit or save.
export const showsResult = (label) => ["good", "bad"].includes((status.get(label) || {}).style);

const savable = (list) => list.filter((p) => p.edited() && p.usable());

export function refresh() {
  const usable = parts.filter((p) => p.usable()),
    busy = parts.some((p) => p.busy()),
    edited = parts.filter((p) => p.edited() && (status.get(p.label) || {}).style !== "warn"); // not sent yet
  $("safety-form").disabled = !usable.length && !busy;
  $("safety-defaults").disabled = !usable.length || busy;
  for (const card of CARDS) {
    const mine = parts.filter((p) => p.card === card);
    $(`save-${card}`).disabled = mine.some((p) => p.busy()) || !savable(mine).length;
  }

  const list = $("safety-status");
  list.replaceChildren();
  for (const p of parts) {
    const s = status.get(p.label);
    if (s && s.text && s.style) list.append(make("li", `${p.label}: ${s.text}`, `tuning-status ${s.style}`));
  }
  if (edited.length)
    list.append(make("li", `Not yet applied: ${edited.map((p) => p.label.toLowerCase()).join(", ")}.`, "tuning-status warn"));
  if (!usable.length && !busy)
    list.append(
      make("li", "Settings unavailable: the controller is offline or does not report them.", "tuning-status muted"),
    );
}

// Check every part first, then send; one prepare() per shared group, with the cards being saved.
function save(todo) {
  const cards = new Set(todo.map((p) => p.card)),
    seen = new Set(),
    ready = [];
  for (const p of todo) {
    if (p.shared && seen.has(p.shared)) continue;
    if (p.shared) seen.add(p.shared);
    ready.push([p, p.prepare(cards)]);
  }
  const bad = ready.filter(([, r]) => r.error);
  if (bad.length) {
    for (const [p, r] of bad) report(p.label, r.error, "bad");
    return;
  }
  for (const p of todo) report(p.label, "");
  for (const [, r] of ready) r.run();
}

for (const card of CARDS)
  $(`save-${card}`).addEventListener("click", () => save(savable(parts.filter((p) => p.card === card))));

$("safety-defaults").addEventListener("click", () => {
  if (!confirm("Restore every safety setting on this tab to the controller's menuconfig defaults?")) return;
  const seen = new Set();
  for (const p of parts.filter((part) => part.usable())) {
    report(p.label, "");
    if (p.shared && seen.has(p.shared)) continue;
    if (p.shared) seen.add(p.shared);
    p.defaults();
  }
});
