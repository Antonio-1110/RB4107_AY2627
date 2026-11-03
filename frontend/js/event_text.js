"use strict";
// What one controller event says, in plain words, for the Event log: "Person left the stove", not
// "State change". The controller reports a state change's reason (the rules table's reported reasons,
// docs/safety_rules.md) and a fault's name (fault_manager); anything not listed here is shown as reported.

// Reported reason -> what happened.
export const REASONS = {
  "boot complete": "Controller started",
  "self-test passed": "Self-test passed",
  "self-test failed": "Self-test failed",
  "self-test timeout": "Self-test timed out",
  "self-test not passed": "Self-test no longer passes",
  "safety-relevant fault active": "Controller fault",
  "heating detected": "Cooking started",
  "heating stopped": "Cooking stopped",
  "person absent": "Person left the stove",
  "person returned": "Person returned",
  "heating stopped while unattended": "Cooking stopped while nobody was there",
  "unattended timeout": "Buzzer warning: nobody returned in time",
  "person returned during warning": "Person returned, buzzer warning ended",
  "heating stopped during warning": "Cooking stopped, buzzer warning ended",
  "shutdown timeout": "Gas shut off",
  "operator reset": "Shut-off reset by operator",
  "fault cleared, no heating": "Fault cleared, no cooking",
  "fault cleared, person present": "Fault cleared, person at the stove",
  "fault cleared, person absent": "Fault cleared, nobody at the stove",
  "shutdown timeout reached during fault": "Gas shut off during a fault",
  "fault persisted": "Gas shut off: fault persisted",
  "controller restarted": "Controller restarted",
};

// Fault name -> the problem, as "<problem>" when raised and "<problem>: cleared" when cleared.
const FAULTS = {
  presence_a_unavailable: "Presence node A: radar not responding",
  presence_b_unavailable: "Presence node B: radar not responding",
  thermal_unavailable: "Thermal node: camera not responding",
  presence_a_node_offline: "Presence node A offline",
  presence_b_node_offline: "Presence node B offline",
  thermal_node_offline: "Thermal node offline",
  shutdown_output_failure: "Gas shut-off output failed",
  self_test_failed: "Self-test failed",
  espnow_invalid_packet: "Invalid packet from a sensor node",
  espnow_link_degraded: "Sensor node radio link degraded",
  espnow_unknown_node: "Packet from an unknown node",
  rtc_failure: "Real-time clock failed",
  network_disconnected: "Wi-Fi connection lost",
  mqtt_disconnected: "MQTT broker connection lost",
  rx_queue_overflow: "Sensor packet queue overflow",
  event_queue_overflow: "Event queue overflow",
  node_restarting: "A sensor node restarted",
};

const sentence = (text) => (text ? text.charAt(0).toUpperCase() + text.slice(1).replace(/_/g, " ") : "");

// "2 min 30 s"
function span(ms) {
  const s = Math.round(ms / 1000);
  return s < 60 ? `${s} s` : `${Math.floor(s / 60)} min${s % 60 ? ` ${s % 60} s` : ""}`;
}

// {title, note}: what happened, and an optional detail line.
export function eventText(event) {
  const detail = event.detail || {},
    safety = detail.safety || {},
    fault = detail.fault;
  if (event.event_type === "fault_raised" || event.event_type === "fault_cleared") {
    const name = fault && fault.name;
    const problem = FAULTS[name] || sentence(name) || "Fault";
    return { title: event.event_type === "fault_cleared" ? `${problem}: cleared` : problem, note: "" };
  }
  if (event.event_type === "controller_restarted") return { title: "Controller restarted", note: "" };
  const reason = safety.reason;
  const title = REASONS[reason] || sentence(reason) || sentence(event.event_type);
  const away = Number(safety.unattended_ms);
  const note =
    (event.event_type === "warning" || event.event_type === "shutdown") && away > 0 ? `After ${span(away)} unattended` : "";
  return { title, note };
}
