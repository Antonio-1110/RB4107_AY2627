"use strict";
// Countdown card on the stall page: one big place that shows where the safety
// timeline is (confirming absence -> unattended -> warning -> shutdown), the
// three conditions it depends on (cooking, person, timer) with their rules, and
// the sensor flags. The controller decides everything shown here; this file only
// reads telemetry. Between telemetry messages the clock is advanced locally from
// the report's age, so it counts smoothly (the next report corrects it).

import { $, make, set } from "./dom.js";
import { human, number } from "./format.js";

const TICK_MS = 250;
const MAX_EXTRAPOLATE_S = 6; // never run ahead of a silent controller for longer than this

let shown = null, // {device, renderedAt}
  timer = null;

const s1 = (value) => `${value.toFixed(1)} s`;
const seconds0 = (value) => (number(value) ? `${Math.round(value)} s` : "—");

// A value that was measured `age` seconds ago and keeps running while `running`.
function runningValue(value, age, running, elapsed) {
  if (!number(value)) return null;
  if (!running) return value;
  const extra = (number(age) ? age : 0) + elapsed;
  return value + Math.min(Math.max(extra, 0), MAX_EXTRAPOLATE_S);
}

// The buzzer and shutdown times in force now: the temperature tier can shorten the base ones.
const limits = (values) => ({
  warn: number(values.effective_warning_seconds) ? values.effective_warning_seconds : values.warning_after_seconds,
  stop: number(values.effective_shutdown_seconds) ? values.effective_shutdown_seconds : values.shutdown_after_seconds,
});
// Temperature tiers: "tier1" is the base times, "tier2" to "tier5" hotter pans with shorter times.
const TIER_LABEL = { tier2: "tier 2, hot", tier3: "tier 3, frying", tier4: "tier 4, hot oil", tier5: "tier 5, oil at its smoke point" };
const shortTier = (tier) => Boolean(TIER_LABEL[tier]);
const strictTier = (tier) => tier === "tier4" || tier === "tier5";

function nodeStatus(sensor) {
  if (sensor.valid && sensor.link === "ONLINE") return ["OK", "normal"];
  if (sensor.link === "ONLINE") return ["SENSOR FAULT", "warning"]; // node reachable, sensor bad: wiring
  return ["OFFLINE", "warning"]; // no packets: power, Wi-Fi, channel
}

// Sensor-node health as small chips beside the stall name: green when fine, amber when not.
function renderSensorChips(values) {
  const host = $("dv-sensors");
  host.replaceChildren();
  let radars = 0;
  for (const [name, sensor] of Object.entries(values.sensors || {})) {
    if (!sensor || !sensor.role || sensor.link === undefined) continue;
    const [label, style] = nodeStatus(sensor);
    const what =
      sensor.role === "presence" ? `Presence node ${String.fromCharCode(65 + radars++)}` : sensor.role === "thermal" ? "Thermal node" : name;
    const chip = make("span", `${what}: ${label === "OK" ? "OK" : label.toLowerCase()}`, `pill ${style === "normal" ? "good" : "warn"}`);
    chip.title = `${name} · ${sensor.role} · ${label}`;
    host.append(chip);
  }
}

// The countdown card follows the safety state. The outputs the controller reports (buzzer pattern, gas supply)
// are not shown again; they appear only when they disagree with the state, which would be a fault worth seeing.
const BUZZER_FOR = { warning: "warning", shutdown: "shutdown", fault: "fault" };
function renderMismatch(values) {
  const state = values.safety_state,
    problems = [];
  if (state && values.alarm_state && values.alarm_state !== (BUZZER_FOR[state] || "clear"))
    problems.push(`the buzzer reports ${human(values.alarm_state).toLowerCase()}`);
  if (state && values.relay_state && (values.relay_state === "isolated") !== (state === "shutdown"))
    problems.push(`the gas supply reports ${values.relay_state === "isolated" ? "off" : "on"}`);
  $("cd-mismatch").hidden = problems.length === 0;
  $("cd-mismatch").textContent = problems.length
    ? `Output mismatch in state ${human(state).toLowerCase()}: ${problems.join(" and ")}. Inspect the controller.`
    : "";
}

// Phase text, clock and bar position for the current state.
function phase(values, age, elapsed) {
  const state = values.safety_state,
    mode = values.shutdown_counts_from,
    { warn: warnAt, stop: stopAt } = limits(values),
    filter = values.presence_filter,
    // the clock stands still while someone is at the stove (the pause)
    unattended = runningValue(values.unattended_seconds, age("unattended_seconds"), values.paused !== true, elapsed);
  const out = { style: "cd-idle", title: "Waiting for controller telemetry", detail: "", clock: null, label: "", fill: 0 };

  if (!state) return out;
  const total = number(stopAt) ? (mode === "WARNING" && number(warnAt) ? warnAt + stopAt : stopAt) : null;
  const sinceWarning = runningValue(values.state_seconds, age("state_seconds"), true, elapsed);

  if (state === "unattended" || state === "warning") {
    // Shutdown counts from the start of UNATTENDED (default) or from the start of the warning.
    let toStop = null,
      position = unattended;
    if (mode === "WARNING") {
      if (state === "warning" && number(stopAt) && number(sinceWarning)) {
        toStop = stopAt - sinceWarning;
        position = (number(warnAt) ? warnAt : 0) + sinceWarning;
      }
    } else if (number(stopAt) && number(unattended)) {
      toStop = stopAt - unattended;
    }
    const toWarn = number(warnAt) && number(unattended) ? warnAt - unattended : null;
    out.fill = total && number(position) ? position / total : 0;
    if (values.hold === "cooling" || values.hold === "return" || values.hold === "falling") {
      // A due buzzer or shutdown is being held back for a reason that the controller reports.
      out.style = "cd-run";
      out.clock = null;
      out.clockText = "hold";
      const stage = state === "warning" ? "Buzzer warning" : "Unattended",
        params = values.params || {};
      if (values.hold === "cooling") {
        // The pan is judged to be cooling: long hold, ends when it levels off hot or cooking stops.
        const tau = number(values.cookware_tau_s) && values.cookware_tau_s > 0 ? 4 * values.cookware_tau_s : null,
          cap = number(params.cooling_hold_max_ms) ? params.cooling_hold_max_ms / 1000 : 3600,
          limit = tau ? Math.min(tau, cap) : cap;
        out.title = `${stage}: on hold, pan cooling down`;
        out.detail =
          "Hold type: COOLING. The controller is sure no heat is going in: the pan is falling at the rate of a pan with the burner off, towards " +
          `room temperature${number(values.cooking_room_c) ? ` (${values.cooking_room_c.toFixed(0)} °C)` : ""}. ` +
          "Ends when the temperature levels off while still hot (the next stage then starts) or when cooking stops (the countdown is cancelled). " +
          `Limit: ${span(limit)} (4 × the cookware time constant, at most the cooling hold maximum).`;
        out.label = "hold: cooling";
      } else if (values.hold === "falling") {
        // The temperature is falling but the trend cannot yet tell cooling from a lowered flame: short hold.
        const max = number(params.falling_hold_max_ms) ? `${Math.round(params.falling_hold_max_ms / 1000)} s` : "the falling hold maximum";
        out.title = `${stage}: on hold, temperature falling`;
        out.detail =
          "Hold type: FALLING. The pan is losing heat quickly for its temperature above the room, but the controller cannot yet tell " +
          "a pan that is cooling down from one on a lowered flame. Ends as soon as the temperature rises again or the trend shows heat " +
          "still going in (the next stage then starts at once), or when the trend decides the pan is cooling (it becomes a COOLING hold). " +
          `Limit: ${max} per countdown.`;
        out.label = "hold: falling";
      } else {
        out.title = `${stage}: on hold, confirming a return`;
        out.detail =
          "Hold type: RETURN. A radar sees someone now and the controller is confirming that they have returned. Ends when the return is " +
          "confirmed (the countdown is cancelled) or the radar loses them (the next stage then starts at once). Limit: the return time plus the gap, a few seconds.";
        out.label = "hold: return";
      }
      return out;
    }
    if (state === "warning") {
      out.style = "cd-warn";
      out.title = "Buzzer warning: gas shut-off pending";
      out.detail = "No return confirmed. The buzzer is sounding; the gas supply is shut off when the countdown reaches 0.";
      out.clock = toStop;
      out.label = "until gas shut-off";
    } else {
      out.style = "cd-run";
      out.title = strictTier(values.tier) ? `Unattended: ${TIER_LABEL[values.tier]}, shortened times` : "Unattended: countdown running";
      out.detail =
        shortTier(values.tier)
          ? `Pan temperature in ${TIER_LABEL[values.tier]}, so shortened times apply: buzzer warning after ${seconds0(warnAt)}, gas shut-off after ${seconds0(stopAt)}. A confirmed return cancels the countdown.`
          : "Cooking detected and nobody confirmed at the stove. A confirmed return cancels the countdown.";
      out.clock = toWarn;
      out.label = "until buzzer warning";
    }
    if (values.paused === true) {
      out.title = `${state === "warning" ? "Buzzer warning" : "Unattended"}: paused, someone passing`;
      out.detail =
        `The countdown is paused while someone is detected (${number(values.pause_used_seconds) ? values.pause_used_seconds.toFixed(0) : "?"} of ` +
        `${number(values.pause_budget_seconds) ? values.pause_budget_seconds.toFixed(0) : "?"} s pause allowance used). ` +
        `Presence for ${number(filter && filter.return_seconds) ? filter.return_seconds : "a few"} s confirms a return and cancels the countdown.`;
      out.label = "paused";
    }
    if (values.presence_unknown === true) out.detail += " Presence unknown (no working radar), so it counts as absent.";
    return out;
  }
  if (state === "shutdown") {
    return { ...out, style: "cd-stop", title: "Shut-off: gas supply off", fill: 1,
      detail: "Latched. Inspect the stove in person, then press Reset shut-off below.", clock: null, label: "reset required" };
  }
  if (state === "fault") {
    return { ...out, style: "cd-warn", title: "Controller fault",
      detail: "The controller's self-test or shut-off output has failed. A sensor failure does not cause this state; the event log names the fault.",
      label: "fault" };
  }
  if (state === "monitoring") {
    const absent = runningValue(values.absent_seconds, age("absent_seconds"), number(values.absent_seconds) && values.absent_seconds > 0, elapsed);
    if (number(absent) && absent > 0 && filter && number(filter.absence_seconds)) {
      out.style = "cd-run";
      out.title = "Cooking: confirming absence";
      out.detail = `Nobody detected for ${s1(Math.min(absent, filter.absence_seconds))} of ${s1(filter.absence_seconds)}. A brief detection does not reset this; only a confirmed return does.`;
      out.clock = Math.max(filter.absence_seconds - absent, 0);
      out.label = "until unattended";
      return out;
    }
    out.style = "cd-ok";
    out.title = values.presence_unknown === true ? "Cooking: presence unknown" : "Cooking: person at the stove";
    out.detail = "The countdown is not running.";
    return out;
  }
  if (state === "idle") {
    out.title = "Idle: no cooking detected";
    out.detail =
      values.presence_unknown === true || values.thermal_lost === true
        ? "A sensor is flagged, but no countdown runs while no cooking is detected."
        : "The countdown starts when the thermal node detects cooking and nobody is at the stove.";
    return out;
  }
  out.title = `Controller ${human(state).toLowerCase()}`;
  return out;
}

// "3 min 4 s" for how long someone has been away.
const span = (value) => (value < 60 ? `${Math.floor(value)} s` : `${Math.floor(value / 60)} min ${Math.floor(value % 60)} s`);

// The three tiles under the countdown: one plain answer each and one short line under it. The buzzer and
// shut-off times are in the countdown card only, so the tiles don't repeat them.
function liveLines(values, age, elapsed) {
  const filter = values.presence_filter,
    cooking = values.cooking_active,
    state = values.safety_state,
    running = state === "unattended" || state === "warning",
    hot = number(values.temperature_c) ? `${Math.round(values.temperature_c)} °C` : "no reading",
    rate = values.cooking_rate_c_per_min,
    trend = number(rate) && Math.abs(rate) >= 0.5 ? `${rate > 0 ? "+" : "−"}${Math.abs(rate).toFixed(1)} °C/min` : "steady";

  let cook = "Not reported",
    cookSub = "";
  if (values.thermal_lost === true) [cook, cookSub] = ["Unknown", "Thermal node lost: flagged; a running countdown continues"];
  else if (cooking === false) [cook, cookSub] = ["Not detected", `Hottest pixel ${hot}`];
  else if (cooking === true) {
    if (values.hold === "cooling" || values.cooking_phase === "cooling")
      [cook, cookSub] = ["Cooling down", `${hot} · no heat input; a due buzzer warning or shut-off is held`];
    else if (values.hold === "falling") [cook, cookSub] = ["Temperature falling", `${hot}, ${trend} · not sure yet whether heat is going in; a due buzzer warning or shut-off waits`];
    else if (values.cooking_phase === "heating") [cook, cookSub] = ["Heating up", `${hot}, ${trend}`];
    else [cook, cookSub] = ["Active, heat input", `${hot}, ${values.cooking_phase === "checking" ? "evaluating the trend" : trend}`];
  }
  set("cd-cooking-live", cook);
  set("cd-cooking-sub", cookSub);

  const unattended = runningValue(values.unattended_seconds, age("unattended_seconds"), values.paused !== true, elapsed),
    absent = runningValue(values.absent_seconds, age("absent_seconds"), number(values.absent_seconds) && values.absent_seconds > 0, elapsed),
    returning = runningValue(values.returning_seconds, age("returning_seconds"), number(values.returning_seconds) && values.returning_seconds > 0, elapsed);
  let person = "Not reported",
    personSub = "";
  if (values.presence_unknown === true)
    [person, personSub] = ["Unknown", cooking === true ? "No working radar: counted as absent while cooking" : "No working radar"];
  else if (values.paused === true)
    [person, personSub] = [
      "Passing by",
      `Countdown paused ${number(values.pause_used_seconds) ? Math.round(values.pause_used_seconds) : "?"} of ${number(values.pause_budget_seconds) ? Math.round(values.pause_budget_seconds) : "?"} s`,
    ];
  else if (values.occupied === true)
    [person, personSub] = [
      "At the stove",
      running && filter && number(returning) && returning > 0
        ? `Confirming return: ${s1(Math.min(returning, filter.return_seconds))} of ${s1(filter.return_seconds)}`
        : "",
    ];
  else if (values.occupied === false) {
    person = values.nearby === true ? "Nearby" : "Absent";
    personSub =
      running && number(unattended)
        ? `Absent for ${span(unattended)}`
        : filter && number(absent) && absent > 0
          ? `Confirming absence: ${s1(Math.min(absent, filter.absence_seconds))} of ${s1(filter.absence_seconds)}`
          : values.nearby === true
            ? "Detected, but not at the stove"
            : "";
  }
  set("cd-person-live", person);
  set("cd-person-sub", personSub);

  const levels = (values.tiers && Array.isArray(values.tiers.levels) && values.tiers.levels) || [],
    level = levels.find((t) => `tier${t.tier}` === values.tier),
    first = levels.find((t) => number(t.temp_c) && t.temp_c > 0);
  let times, timesSub;
  if (state === "shutdown") [times, timesSub] = ["Gas shut off", "Awaiting reset after an in-person check of the stove"];
  else if (state === "fault") [times, timesSub] = ["Controller fault", "Gas shut-off follows if the fault persists"];
  else if (running && values.hold === "cooling") [times, timesSub] = ["On hold", "Pan cooling"];
  else if (running && values.hold === "falling") [times, timesSub] = ["On hold", "Temperature falling"];
  else if (running && values.hold === "return") [times, timesSub] = ["On hold", "Confirming return"];
  else if (values.paused === true) [times, timesSub] = ["Paused", "Someone passing"];
  else if (cooking !== true) [times, timesSub] = ["Not running", "Starts when cooking is detected and nobody is at the stove"];
  else if (shortTier(values.tier))
    [times, timesSub] = ["Shortened times", `${TIER_LABEL[values.tier]}${level ? `: pan at ${level.temp_c} °C or above` : ""}`];
  else [times, timesSub] = ["Base times", first ? `Pan below ${first.temp_c} °C` : "Tier 1 times"];
  set("cd-timer-live", times);
  set("cd-timer-sub", timesSub);

  // The radar state and distance rules (a placeholder): what each radar's recent target looks like.
  const rules = values.radar_rules,
    VERDICT = { none: "no target", passing: "passing", dwelling: "at the stove" };
  const radars = Object.entries(values.sensors || {})
    .filter(([, sensor]) => sensor && sensor.verdict)
    .map(
      ([name, sensor]) =>
        `${name}: ${VERDICT[sensor.verdict] || sensor.verdict}` +
        (sensor.verdict !== "none" || sensor.episode_seconds
          ? ` (${number(sensor.episode_seconds) ? sensor.episode_seconds.toFixed(1) : "?"} s, ${number(sensor.stationary_seconds) ? sensor.stationary_seconds.toFixed(1) : "?"} s stationary, span ${sensor.span_cm ?? "?"} cm)`
          : ""),
    );
  set(
    "cd-person-radars",
    rules
      ? `Radar classification (provisional; ${rules.mode === "observe" ? "reported, not used" : rules.mode === "enforce" ? "enforced: a radar that only detects someone passing does not count as at the stove" : "off"}): ` +
          (radars.length ? radars.join(" · ") : "no radar episode recorded.")
      : "",
  );
}

function draw() {
  if (!shown) return;
  const { device, renderedAt } = shown,
    values = device.values || {},
    live = device.connection === "online", // a silent controller's clock is frozen, not extrapolated
    elapsed = live ? (Date.now() - renderedAt) / 1000 : 0,
    age = (key) => (live ? (device.field_age_seconds || {})[key] : 0);
  const view = phase(values, age, elapsed);
  if (!live && view.label) view.label += " · last known";
  $("countdown-card").className = `card countdown-card ${view.style}`;
  set("cd-phase", view.title);
  set("cd-detail", view.detail);
  set("cd-seconds", number(view.clock) ? Math.max(Math.ceil(view.clock), 0).toString() : view.clockText || "—");
  set("cd-unit", number(view.clock) ? "s" : "");
  set("cd-clock-label", view.label);
  // No seconds to count (idle, shut down, fault): no big clock, rather than a large dash.
  $("cd-seconds").closest(".cd-clock").hidden = !number(view.clock) && !view.clockText;
  const { warn: warnAt, stop: stopAt } = limits(values),
    total = number(stopAt) ? (values.shutdown_counts_from === "WARNING" && number(warnAt) ? warnAt + stopAt : stopAt) : null;
  $("cd-fill").style.width = `${Math.min(Math.max(view.fill, 0), 1) * 100}%`;
  for (const [id, at] of [["cd-mark-warn", warnAt], ["cd-mark-stop", total]]) {
    $(id).hidden = !(number(at) && total);
    if (number(at) && total) $(id).style.left = `${(at / total) * 100}%`;
  }
  set("cd-scale-warn", `Buzzer ${number(warnAt) ? Math.round(warnAt) + " s" : "—"}`);
  set("cd-scale-stop", `Shut-off ${number(total) ? Math.round(total) + " s" : "—"}`);
  liveLines(values, age, elapsed);
}

export function renderCountdown(device) {
  shown = { device, renderedAt: Date.now() };
  const values = device.values || {};
  renderSensorChips(values);
  renderMismatch(values);
  $("cd-reset").hidden = values.manual_reset_required !== true;
  draw();
  if (timer === null) timer = setInterval(draw, TICK_MS);
}
