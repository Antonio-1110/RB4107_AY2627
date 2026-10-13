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
const TIER_LABEL = { elevated: "hot pan", critical: "hot oil range" };

function nodeStatus(sensor) {
  if (sensor.valid && sensor.link === "ONLINE") return ["OK", "normal"];
  if (sensor.link === "ONLINE") return ["SENSOR PROBLEM", "warning"]; // node reachable, sensor bad: wiring
  return ["OFFLINE", "warning"]; // no packets: power, Wi-Fi, channel
}

function renderFlags(values) {
  const host = $("cd-flags");
  host.replaceChildren();
  for (const [name, sensor] of Object.entries(values.sensors || {})) {
    if (!sensor || !sensor.role || sensor.link === undefined) continue;
    const [label, style] = nodeStatus(sensor);
    host.append(make("span", `${name} · ${sensor.role} · ${label}`, `state-badge ${style}`));
  }
  if (values.tier && values.tier !== "normal") {
    host.append(
      make(
        "span",
        `Temperature tier: ${TIER_LABEL[values.tier] || values.tier} · shorter limits (buzzer ${seconds0(limits(values).warn)}, shutdown ${seconds0(limits(values).stop)})`,
        `state-badge ${values.tier === "critical" ? "critical" : "warning"}`,
      ),
    );
  }
  if (values.presence_unknown === true) {
    host.append(
      make(
        "span",
        values.cooking_active === true
          ? "Presence sensor unknown · counted as absent while cooking"
          : "Presence sensor unknown · no timer while nothing is cooking",
        "state-badge warning",
      ),
    );
  }
  if (values.thermal_lost === true) {
    host.append(make("span", "Thermal sensor lost · flag only, no timer started", "state-badge warning"));
  }
  host.hidden = host.childElementCount === 0;
}

// Phase text, clock and bar position for the current state.
function phase(values, age, elapsed) {
  const state = values.safety_state,
    mode = values.shutdown_counts_from,
    { warn: warnAt, stop: stopAt } = limits(values),
    filter = values.presence_filter,
    unattended = runningValue(values.unattended_seconds, age("unattended_seconds"), true, elapsed);
  const out = { style: "cd-idle", title: "Waiting for the controller", detail: "", clock: null, label: "", fill: 0 };

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
    if (values.hold === "cooling" || values.hold === "return") {
      // A due buzzer or shutdown is being held back for a reason that the controller reports.
      out.style = "cd-run";
      out.clock = null;
      out.clockText = "hold";
      if (values.hold === "cooling") {
        out.title = `${state === "warning" ? "Warning" : "Unattended"}: held while the pan cools`;
        out.detail =
          "No heat is going in: the temperature is heading for " +
          `${number(values.cooking_settle_c) ? values.cooking_settle_c.toFixed(0) + " °C" : "the room temperature"}. ` +
          "The next step comes when the trend flattens out hot, or it ends when the pan drops below the cooking temperature.";
        out.label = "held: cooling";
      } else {
        out.title = `${state === "warning" ? "Warning" : "Unattended"}: held, a return is being confirmed`;
        out.detail = "The radar sees someone and is confirming that they are really back. At most a few seconds.";
        out.label = "held: return";
      }
      return out;
    }
    if (state === "warning") {
      out.style = "cd-warn";
      out.title = "Warning: shutdown is coming";
      out.detail = "Nobody has returned. The buzzer is sounding; the gas is cut when the clock reaches 0.";
      out.clock = toStop;
      out.label = "until shutdown";
    } else {
      out.style = "cd-run";
      out.title = values.tier === "critical" ? "Unattended: hot oil range, short limits" : "Unattended: timer running";
      out.detail =
        values.tier && values.tier !== "normal"
          ? `The pan is hot (${TIER_LABEL[values.tier]}), so the limits are short: buzzer after ${seconds0(warnAt)}, shutdown after ${seconds0(stopAt)}. A person returning for the "return after" time cancels it.`
          : "Cooking and no one confirmed at the stove. A person returning for the \"return after\" time cancels it.";
      out.clock = toWarn;
      out.label = "until the buzzer";
    }
    if (values.presence_unknown === true) out.detail += " Presence sensor unknown, so presence counts as absent.";
    return out;
  }
  if (state === "shutdown") {
    return { ...out, style: "cd-stop", title: "Shutdown: gas supply cut", fill: 1,
      detail: "Latched. Check the stove, then use Reset shutdown in System status.", clock: null, label: "reset required" };
  }
  if (state === "fault") {
    return { ...out, style: "cd-warn", title: "Controller fault",
      detail: "The controller's self-test or shutdown output failed. A lost sensor does not cause this; see Faults.",
      label: "fault" };
  }
  if (state === "monitoring") {
    const absent = runningValue(values.absent_seconds, age("absent_seconds"), number(values.absent_seconds) && values.absent_seconds > 0, elapsed);
    if (number(absent) && absent > 0 && filter && number(filter.absence_seconds)) {
      out.style = "cd-run";
      out.title = "Cooking: confirming that nobody is there";
      out.detail = `No one seen for ${s1(Math.min(absent, filter.absence_seconds))} of ${s1(filter.absence_seconds)}. A short blip does not restart this; a confirmed return does.`;
      out.clock = Math.max(filter.absence_seconds - absent, 0);
      out.label = "until unattended";
      return out;
    }
    out.style = "cd-ok";
    out.title = values.presence_unknown === true ? "Cooking: presence unknown" : "Cooking: someone is there";
    out.detail = "The timer is not running.";
    return out;
  }
  if (state === "idle") {
    out.title = "Idle: nothing is cooking";
    out.detail =
      values.presence_unknown === true || values.thermal_lost === true
        ? "A sensor flag is up, but no timer runs while nothing is cooking."
        : "The timer starts when the thermal camera sees cooking and nobody is there.";
    return out;
  }
  out.title = `Controller ${human(state).toLowerCase()}`;
  return out;
}

function liveLines(values, age, elapsed) {
  const filter = values.presence_filter,
    cooking = values.cooking_active;
  const hot = number(values.temperature_c) ? `${values.temperature_c.toFixed(1)} °C` : "no reading";
  const PHASE = {
    heating: "heating up",
    holding: "heat going in (steady)",
    cooling: "only cooling down",
    checking: "judging the trend",
  };
  const phase = values.cooking_phase && PHASE[values.cooking_phase] ? ` · ${PHASE[values.cooking_phase]}` : "";
  const trend =
    values.cooking_phase && values.cooking_phase !== "off" && number(values.cooking_rate_c_per_min)
      ? ` · ${values.cooking_rate_c_per_min > 0 ? "+" : ""}${values.cooking_rate_c_per_min.toFixed(1)} °C/min` +
        (number(values.cooking_settle_c) ? ` heading for ${values.cooking_settle_c.toFixed(0)} °C` : "")
      : "";
  set(
    "cd-cooking-live",
    values.thermal_lost === true
      ? "Cooking: unknown · thermal sensor lost"
      : `Cooking: ${cooking === true ? "yes" : cooking === false ? "no" : "not reported"}${phase} · hottest pixel ${hot}${trend}` +
          (number(values.cooking_on_c) ? ` (from ${values.cooking_on_c.toFixed(1)} °C, over below ${number(values.cooking_off_c) ? values.cooking_off_c.toFixed(1) : "—"} °C)` : ""),
  );
  let person = values.occupied === true ? "present" : values.occupied === false ? "absent" : "unknown";
  const absent = runningValue(values.absent_seconds, age("absent_seconds"), number(values.absent_seconds) && values.absent_seconds > 0, elapsed),
    returning = runningValue(values.returning_seconds, age("returning_seconds"), number(values.returning_seconds) && values.returning_seconds > 0, elapsed);
  if (filter && number(absent) && absent > 0) person += ` · absence counted ${s1(Math.min(absent, filter.absence_seconds))} of ${s1(filter.absence_seconds)}`;
  else if (filter && number(returning) && returning > 0 && ["unattended", "warning"].includes(values.safety_state))
    person += ` · return counted ${s1(Math.min(returning, filter.return_seconds))} of ${s1(filter.return_seconds)}`;
  set("cd-person-live", `Person: ${person}`);
  set(
    "cd-timer-live",
    number(values.unattended_seconds)
      ? `Timer: ${["unattended", "warning"].includes(values.safety_state) ? `unattended ${s1(runningValue(values.unattended_seconds, age("unattended_seconds"), true, elapsed))}` : "not running"} · buzzer at ${seconds0(limits(values).warn)}, shutdown at ${seconds0(limits(values).stop)}` +
          (values.tier && values.tier !== "normal" ? ` (${TIER_LABEL[values.tier]})` : "")
      : "Timer: not reported",
  );
  const tiers = values.tiers;
  set(
    "cd-timer-tiers",
    tiers && tiers.elevated && tiers.critical
      ? `By temperature: below ${tiers.elevated.temp_c} °C the base times · ${tiers.elevated.temp_c} °C and up: buzzer ${seconds0(tiers.elevated.warning_seconds)}, shutdown ${seconds0(tiers.elevated.shutdown_seconds)} · ` +
          `${tiers.critical.temp_c} °C and up${number(tiers.critical.rate_c_per_min) && tiers.critical.rate_c_per_min > 0 ? `, or rising ${tiers.critical.rate_c_per_min} °C/min above ${tiers.critical.rate_min_temp_c} °C` : ""}: buzzer ${seconds0(tiers.critical.warning_seconds)}, shutdown ${seconds0(tiers.critical.shutdown_seconds)}`
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
  const { warn: warnAt, stop: stopAt } = limits(values),
    total = number(stopAt) ? (values.shutdown_counts_from === "WARNING" && number(warnAt) ? warnAt + stopAt : stopAt) : null;
  $("cd-fill").style.width = `${Math.min(Math.max(view.fill, 0), 1) * 100}%`;
  for (const [id, at] of [["cd-mark-warn", warnAt], ["cd-mark-stop", total]]) {
    $(id).hidden = !(number(at) && total);
    if (number(at) && total) $(id).style.left = `${(at / total) * 100}%`;
  }
  set("cd-scale-warn", `Warning ${number(warnAt) ? Math.round(warnAt) + " s" : "—"}`);
  set("cd-scale-stop", `Shutdown ${number(total) ? Math.round(total) + " s" : "—"}`);
  liveLines(values, age, elapsed);
}

export function renderCountdown(device) {
  shown = { device, renderedAt: Date.now() };
  renderFlags(device.values || {});
  draw();
  if (timer === null) timer = setInterval(draw, TICK_MS);
}
