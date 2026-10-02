"use strict";
// Main dashboard logic: keeps the current state, polls the API and renders
// the overview (all stalls) and detail (one stall) views.

import { POLL_MS } from "./config.js";
import { fetchEvents, fetchFleet, fetchHistory } from "./api.js";
import { drawTemperatureChart } from "./chart.js";
import { $, make, makeBadge, pill, set } from "./dom.js";
import { human, locationLine, number, seconds, when } from "./format.js";
import { renderTuning } from "./tuning.js";

let fleet = { devices: [], stalls: [], alerts: [], summary: {}, catalog: {}, worker: {} };
let selectedStall = "",
  selectedDevice = "",
  current = null,
  points = [],
  lastHistoryAt = 0,
  busy = false,
  activeView = "overview",
  initialRouteApplied = false;
function freshness(device, key, explicitAge) {
  const age = explicitAge === undefined ? device.field_age_seconds[key] : explicitAge;
  if (age == null) return "No live timestamp · retained or not reported";
  const stale = age > device.stale_after_seconds || device.connection !== "online";
  return `${stale ? "Last known · " : "Updated "}${Math.floor(age)} s ago`;
}
function metric(id, key, text) {
  set(id, text);
  set(`age-${id}`, freshness(current, key));
  const age = current.field_age_seconds[key];
  $(`card-${id}`).classList.toggle(
    "stale",
    age == null || age > current.stale_after_seconds || current.connection !== "online",
  );
}
function statusValue(id, key, value) {
  const age = current.field_age_seconds[key],
    stale = age != null && (age > current.stale_after_seconds || current.connection !== "online");
  set(id, value + (stale ? " · last known" : ""));
  $(id).title = freshness(current, key);
}
function showView(view) {
  activeView = view;
  $("overview-view").hidden = view !== "overview";
  $("detail-view").hidden = view !== "detail";
  $("overview-tab").classList.toggle("active", view === "overview");
  $("detail-tab").classList.toggle("active", view === "detail");
  if (view === "overview") {
    set("page-title", "All-stall safety overview");
    set("page-subtitle", "Airport terminals, zones and external sites in one view.");
    history.replaceState(null, "", "#overview");
  } else {
    const stall = fleet.stalls.find((item) => item.stall_id === selectedStall);
    set("page-title", stall ? stall.stall_name : "Stall detail");
    set(
      "page-subtitle",
      stall ? locationLine(stall.location) : "Device telemetry and event history.",
    );
    history.replaceState(null, "", `#stall=${encodeURIComponent(selectedStall)}`);
    setTimeout(drawChart, 0);
  }
}
function openStall(stallId, deviceId = "") {
  const stall = fleet.stalls.find((item) => item.stall_id === stallId);
  if (!stall) return;
  selectedStall = stallId;
  selectedDevice = stall.device_ids.includes(deviceId) ? deviceId : stall.device_ids[0];
  lastHistoryAt = 0;
  points = [];
  renderEventsMessage("Loading station history…");
  set("chart-caption", "Loading temperature history…");
  $("detail-tab").disabled = false;
  showView("detail");
  renderDetail();
  refreshDetailData(true);
  window.scrollTo({ top: 0, behavior: "smooth" });
}
function applyInitialRoute() {
  if (initialRouteApplied) return;
  initialRouteApplied = true;
  const match = location.hash.match(/^#stall=(.+)$/);
  if (!match) return;
  let stallId;
  try {
    stallId = decodeURIComponent(match[1]);
  } catch {
    return;
  }
  const stall = fleet.stalls.find((item) => item.stall_id === stallId);
  if (!stall) return;
  selectedStall = stallId;
  selectedDevice = stall.device_ids[0];
  lastHistoryAt = 0;
  points = [];
  $("detail-tab").disabled = false;
  showView("detail");
}
function renderGlobalAlerts() {
  const host = $("global-alerts"),
    existing = new Map([...host.children].map((card) => [card.dataset.alertKey, card])),
    keep = new Set();
  // Reserve the sticky banner for firmware-reported isolation awaiting a physical reset.
  // Ordinary warnings/faults stay available in the overview, detail view and event log.
  const resetAlerts = fleet.alerts.filter(
    (alert) =>
      alert.values.relay_state === "isolated" && alert.values.manual_reset_required === true,
  );
  host.hidden = resetAlerts.length === 0;
  for (const [index, alert] of resetAlerts.entries()) {
    const key = alert.device_id;
    keep.add(key);
    let card = existing.get(key);
    const device = fleet.devices.find((item) => item.device_id === key);
    const lastKnown =
      alert.state.last_known ||
      alert.connection !== "online" ||
      ["relay_state", "manual_reset_required"].some((field) => {
        const age = device?.field_age_seconds?.[field];
        return !number(age) || age > device.stale_after_seconds;
      });
    const stateSignature = `${alert.severity}|${alert.title}|${lastKnown}|${alert.connection}`;
    if (!card) {
      card = make("article", null, "global-alert");
      card.dataset.alertKey = key;
      card.setAttribute("role", "alert");
      const content = make("div", null, "alert-content"),
        headline = make("div", null, "alert-headline");
      headline.append(
        make("strong", null, "alert-title"),
        makeBadge("LAST KNOWN — TELEMETRY NOT CURRENT", "last-known"),
      );
      content.append(
        headline,
        make("p", null, "alert-stall"),
        make("p", null, "alert-location"),
        make("p", null, "alert-facts"),
      );
      const button = make("button", "Open stall details", "alert-button");
      button.type = "button";
      card.append(make("div", null, "alert-icon"), content, button);
    } else
      card.setAttribute("role", card.dataset.alertState === stateSignature ? "group" : "alert");
    card.dataset.alertState = stateSignature;
    card.className = `global-alert global-alert-${alert.severity}`;
    card.querySelector(".alert-icon").textContent = alert.severity === "critical" ? "!" : "△";
    card.querySelector(".alert-title").textContent = "SUPPLY ISOLATED · MANUAL RESET REQUIRED";
    card.querySelector(".last-known").hidden = !lastKnown;
    card.querySelector(".alert-stall").textContent = `${alert.stall_name} · ${alert.station_name}`;
    card.querySelector(".alert-location").textContent = locationLine(alert.location);
    card.querySelector(".alert-facts").textContent =
      `Presence ${alert.values.occupied === true ? "present" : alert.values.occupied === false ? "absent" : "unknown"} · ${number(alert.values.temperature_c) ? alert.values.temperature_c.toFixed(1) + " °C" : "Temperature unknown"} · unattended ${seconds(alert.values.unattended_seconds)} · relay ${human(alert.values.relay_state)} · ${human(alert.connection)} · report ${when(alert.last_seen_at)}`;
    card.querySelector(".alert-button").onclick = () => openStall(alert.stall_id, alert.device_id);
    if (host.children[index] !== card) host.insertBefore(card, host.children[index] || null);
  }
  for (const [key, card] of existing) if (!keep.has(key)) card.remove();
}
function renderSummary() {
  const summary = fleet.summary || {};
  for (const [id, key] of [
    ["summary-total", "total"],
    ["summary-critical", "critical"],
    ["summary-warning", "warning"],
    ["summary-fault", "fault"],
    ["summary-unknown", "unknown"],
    ["summary-connectivity", "connectivity_issues"],
    ["summary-normal", "normal"],
  ])
    set(id, summary[key] || 0);
}
function syncSelect(select, values, allLabel) {
  const previous = select.value,
    unique = [...new Set(values.filter(Boolean))].sort(),
    desired = ["", ...unique],
    current = [...select.options].map((option) => option.value);
  if (
    desired.length === current.length &&
    desired.every((value, index) => value === current[index])
  )
    return;
  select.replaceChildren(new Option(allLabel, ""));
  for (const value of unique) select.add(new Option(value, value));
  select.value = [...select.options].some((option) => option.value === previous) ? previous : "";
}
function renderFilterOptions() {
  syncSelect(
    $("site-filter"),
    fleet.stalls.map((stall) => stall.location.site_name),
    "All sites",
  );
  syncSelect(
    $("building-filter"),
    fleet.stalls.map((stall) => stall.location.building),
    "All buildings",
  );
  syncSelect(
    $("zone-filter"),
    fleet.stalls.map((stall) => stall.location.access_zone),
    "All zones",
  );
}
function filteredStalls() {
  const query = $("stall-search").value.trim().toLowerCase(),
    site = $("site-filter").value,
    building = $("building-filter").value,
    zone = $("zone-filter").value,
    severity = $("severity-filter").value;
  return fleet.stalls.filter((stall) => {
    const searchable = [
      stall.stall_name,
      stall.stall_id,
      ...stall.device_ids,
      ...Object.values(stall.location),
    ]
      .join(" ")
      .toLowerCase();
    const severityMatch =
      !severity ||
      stall.severity === severity ||
      (severity === "connectivity" && stall.connection !== "online");
    return (
      (!query || searchable.includes(query)) &&
      (!site || stall.location.site_name === site) &&
      (!building || stall.location.building === building) &&
      (!zone || stall.location.access_zone === zone) &&
      severityMatch
    );
  });
}
function stallButton(stall) {
  const button = make("button", null, `stall-tile severity-${stall.severity}`);
  button.type = "button";
  button.dataset.stallFocus = `tile:${stall.stall_id}`;
  button.addEventListener("click", () => openStall(stall.stall_id));
  const head = make("div", null, "stall-tile-head");
  head.append(
    make("strong", stall.stall_name),
    makeBadge(
      `${human(stall.severity)}${stall.state_last_known ? " · last known" : ""}`,
      stall.severity,
    ),
  );
  const place = make(
    "p",
    `${stall.location.access_zone} · ${stall.location.level} · ${stall.location.area}`,
  );
  const facts = make(
    "p",
    `${number(stall.temperature_c) ? stall.temperature_c.toFixed(1) + " °C" : "—"} · ${seconds(stall.unattended_seconds)} unattended · ${human(stall.connection)}`,
    "stall-tile-facts",
  );
  button.append(head, place, facts);
  return button;
}
function renderLocationGroups(stalls) {
  const host = $("location-groups");
  host.replaceChildren();
  set("visible-count", `${stalls.length} stalls`);
  if (!stalls.length) {
    host.append(make("p", "No stalls match the selected filters.", "empty-cell"));
    return;
  }
  const groups = new Map();
  for (const stall of stalls) {
    const key = [
      stall.location.site_name,
      stall.location.building,
      stall.location.access_zone,
      stall.location.level,
    ].join("|");
    if (!groups.has(key)) groups.set(key, []);
    groups.get(key).push(stall);
  }
  for (const [key, members] of groups) {
    const [site, building, zone, level] = key.split("|");
    const section = make("section", null, "location-group");
    const heading = make("div", null, "location-group-heading");
    heading.append(make("h3", `${building} · ${zone} · ${level}`), make("span", site));
    const grid = make("div", null, "stall-grid");
    for (const stall of members) grid.append(stallButton(stall));
    section.append(heading, grid);
    host.append(section);
  }
}
function renderRoster(stalls) {
  const body = $("stall-rows");
  body.replaceChildren();
  if (!stalls.length) {
    const row = body.insertRow(),
      cell = row.insertCell();
    cell.colSpan = 8;
    cell.className = "empty-cell";
    cell.textContent = "No stalls match the selected filters.";
    return;
  }
  for (const stall of stalls) {
    const row = body.insertRow();
    row.className = `roster-row severity-${stall.severity}`;
    const locationCell = row.insertCell(),
      open = make("button", stall.stall_name, "stall-link");
    open.type = "button";
    open.dataset.stallFocus = `row:${stall.stall_id}`;
    open.addEventListener("click", () => openStall(stall.stall_id));
    locationCell.append(
      open,
      make(
        "small",
        `${stall.location.building} · ${stall.location.access_zone} · ${stall.location.level} · ${stall.location.area}`,
      ),
    );
    row
      .insertCell()
      .append(
        makeBadge(
          `${stall.state_title}${stall.state_last_known && !stall.state_title.includes("LAST-KNOWN") ? " · LAST KNOWN" : ""}`,
          stall.severity,
        ),
      );
    row.insertCell().textContent =
      stall.occupied === "mixed"
        ? "Mixed"
        : stall.occupied === true
          ? "Present"
          : stall.occupied === false
            ? "Absent"
            : "Unknown";
    row.insertCell().textContent = number(stall.temperature_c)
      ? `${stall.temperature_c.toFixed(1)} °C${stall.temperature_fresh ? "" : " · last known"}`
      : "—";
    row.insertCell().textContent = seconds(stall.unattended_seconds);
    row.insertCell().textContent = human(stall.relay_state);
    row
      .insertCell()
      .append(
        makeBadge(
          human(stall.connection),
          stall.connection === "online" ? "normal" : "connectivity",
        ),
      );
    row.insertCell().textContent = when(stall.last_seen_at);
  }
}
function renderOverview() {
  const focusKey = document.activeElement?.dataset?.stallFocus;
  renderSummary();
  renderFilterOptions();
  const stalls = filteredStalls();
  renderLocationGroups(stalls);
  renderRoster(stalls);
  set("catalog-notice", fleet.catalog.notice || "");
  if (focusKey)
    [...document.querySelectorAll("[data-stall-focus]")]
      .find((node) => node.dataset.stallFocus === focusKey)
      ?.focus({ preventScroll: true });
}
function renderDetailAlert(device) {
  const host = $("detail-alert"),
    state = device.display_state;
  host.replaceChildren();
  host.hidden = !state.active;
  if (!state.active) return;
  host.className = `detail-alert detail-alert-${state.level}`;
  const title = make("strong", state.title);
  const text = make(
    "p",
    state.last_known
      ? "This is a last-known active state. Current safety cannot be confirmed because telemetry is not fresh."
      : "Local ESP32 safety action is active. Respond at the physical location; this page cannot silence or reset it.",
  );
  host.append(title, text);
}
function renderDevice(device) {
  current = device;
  const value = device.values;
  pill(
    "device-status",
    `Device ${human(device.connection).toLowerCase()}`,
    device.connection === "online" ? "good" : "warn",
  );
  $("demo-badge").hidden = value.simulation !== true;
  $("test-timers-badge").hidden = value.test_timers !== true;
  set(
    "thermal-extra",
    `Hot region ${number(value.hot_region_c) ? value.hot_region_c.toFixed(1) + " °C" : "—"} · rate ${number(value.rate_c_per_min) ? value.rate_c_per_min.toFixed(1) + " °C/min" : "—"}`,
  );
  metric(
    "presence",
    "occupied",
    value.occupied === true ? "Present" : value.occupied === false ? "Absent" : "Unknown",
  );
  metric(
    "temperature",
    "temperature_c",
    number(value.temperature_c) ? `${value.temperature_c.toFixed(1)} °C` : "—",
  );
  metric("safety", "safety_state", human(value.safety_state));
  metric("alarm", "alarm_state", human(value.alarm_state));
  $("card-alarm").classList.toggle(
    "danger",
    ["warning", "shutdown", "fault"].includes(value.alarm_state),
  );
  $("card-safety").classList.toggle(
    "danger",
    ["warning", "shutdown", "fault"].includes(value.safety_state),
  );
  statusValue("relay", "relay_state", human(value.relay_state));
  statusValue(
    "reset-required",
    "manual_reset_required",
    value.manual_reset_required === true
      ? "Yes · at device"
      : value.manual_reset_required === false
        ? "No"
        : "Unknown",
  );
  statusValue("timer", "unattended_seconds", seconds(value.unattended_seconds));
  statusValue("warning-setting", "warning_after_seconds", seconds(value.warning_after_seconds));
  statusValue(
    "shutdown-setting",
    "shutdown_after_seconds",
    number(value.shutdown_after_seconds)
      ? `${seconds(value.shutdown_after_seconds)} from ${human(value.shutdown_counts_from).toLowerCase()}`
      : "—",
  );
  statusValue("uptime", "uptime_seconds", seconds(value.uptime_seconds));
  set(
    "raw-data",
    JSON.stringify(
      {
        location: device.location,
        display_state: device.display_state,
        values: value,
        field_updated_at: device.field_updated_at,
      },
      null,
      2,
    ),
  );
  renderSensors(value.sensors || {});
  renderTuning(device);
  renderDetailAlert(device);
}
function renderDetail() {
  const stall = fleet.stalls.find((item) => item.stall_id === selectedStall);
  if (!stall) {
    showView("overview");
    return;
  }
  const members = fleet.devices.filter((device) => stall.device_ids.includes(device.device_id));
  if (!members.length) return;
  if (!members.some((device) => device.device_id === selectedDevice))
    selectedDevice = members[0].device_id;
  const select = $("device-select");
  select.replaceChildren();
  for (const device of members)
    select.add(
      new Option(`${device.location.station_name} · ${device.device_id}`, device.device_id),
    );
  select.value = selectedDevice;
  const device = members.find((item) => item.device_id === selectedDevice);
  set("detail-stall-name", stall.stall_name);
  set("detail-location-line", locationLine(stall.location));
  set("detail-response-route", `Response route: ${stall.location.response_route}`);
  $("detail-demo").hidden = !stall.location.demo_only;
  const mapUrl = stall.location.map_url || fleet.catalog.map_url || "";
  $("official-map").hidden = !mapUrl;
  if (mapUrl) $("official-map").href = mapUrl;
  renderDevice(device);
  showView("detail");
}
function renderSensors(sensors) {
  const rows = $("sensor-rows");
  rows.replaceChildren();
  const entries = Object.entries(sensors);
  set("sensor-count", `${entries.length} sensors`);
  if (!entries.length) {
    const row = rows.insertRow(),
      cell = row.insertCell();
    cell.colSpan = 4;
    cell.className = "empty-cell";
    cell.textContent = "No sensors reported";
  }
  for (const [id, sensor] of entries) {
    const row = rows.insertRow(),
      readings = sensor && typeof sensor === "object" ? sensor : {};
    const summary = Object.entries(readings)
      .filter(
        ([key]) => !["type", "device_id", "message_id", "timestamp", "frame", "c4002", "c4002_live"].includes(key),
      )
      .map(
        ([key, value]) =>
          `${key}: ${typeof value === "object" ? JSON.stringify(value).slice(0, 80) : value}`,
      )
      .join(" · ");
    const ages = Object.keys(readings)
        .filter((key) => key !== "type")
        .map((key) => current.field_age_seconds[`sensors.${id}.${key}`])
        .filter(number),
      sensorAge = ages.length ? Math.max(...ages) : current.field_age_seconds[`sensors.${id}`];
    for (const value of [
      id,
      readings.type || "—",
      summary || "—",
      freshness(current, `sensors.${id}`, sensorAge),
    ])
      row.insertCell().textContent = value;
  }
}
function renderEvents(events, truncated = false) {
  const list = $("events");
  list.replaceChildren();
  set("event-count", `${events.length}${truncated ? "+" : ""}`);
  if (!events.length) {
    const item = make("li", "No events reported", "empty-cell");
    list.append(item);
    return;
  }
  for (const event of events) {
    const item = make("li"),
      detail = make("div"),
      title = make("strong", human(event.event_type)),
      timestamp = make("time", when(event.received_at));
    detail.append(title);
    if (event.detail.note) detail.append(make("p", String(event.detail.note), "event-note"));
    timestamp.dateTime = event.received_at;
    item.append(detail, timestamp);
    list.append(item);
  }
}
function renderEventsMessage(message, count = "—") {
  const list = $("events");
  list.replaceChildren(make("li", message, "empty-cell"));
  set("event-count", count);
}
function drawChart() {
  drawTemperatureChart(
    $("temperature-chart"),
    $("chart-empty"),
    points,
    Math.max(30, current?.stale_after_seconds || 15),
  );
}
async function refreshDetailData(force = false) {
  if (activeView !== "detail" || !selectedDevice || (!force && Date.now() - lastHistoryAt <= 5000))
    return;
  const requested = selectedDevice;
  try {
    const [historyData, eventData] = await Promise.all([
      fetchHistory(requested, $("history-hours").value),
      fetchEvents(requested),
    ]);
    if (requested !== selectedDevice) return;
    points = historyData.points;
    drawChart();
    renderEvents(eventData.events, eventData.truncated);
    lastHistoryAt = Date.now();
    set(
      "chart-caption",
      `${points.length} samples · server receipt time${historyData.truncated ? " · latest 360 samples" : ""}`,
    );
  } catch (error) {
    if (requested !== selectedDevice) return;
    points = [];
    drawChart();
    renderEventsMessage("History unavailable; retrying automatically.", "Unavailable");
    set("chart-caption", "Temperature history unavailable; live fleet data is unaffected.");
    lastHistoryAt = Date.now();
    console.warn("Detail history refresh failed:", error.message);
  }
}
function updateSystemStatus() {
  document.body.classList.remove("transport-lost");
  pill("server-status", "Server connected", "good");
  const worker = fleet.worker || {};
  const workerText =
    worker.mode === "demo-direct"
      ? worker.connected
        ? "Demo direct · MQTT bypassed"
        : "Demo stopped"
      : worker.connected
        ? "MQTT subscribed"
        : worker.running
          ? "MQTT disconnected"
          : "MQTT worker stopped";
  pill(
    "worker-status",
    workerText,
    worker.mode === "demo-direct" && worker.connected ? "demo" : worker.connected ? "good" : "warn",
  );
  const catalogStatus = fleet.catalog.status,
    catalogText =
      catalogStatus === "demo_reference"
        ? "Demo reference locations"
        : catalogStatus === "unavailable"
          ? "Location catalogue unavailable"
          : catalogStatus === "invalid"
            ? "Location catalogue invalid"
            : catalogStatus
              ? "Location catalogue loaded"
              : "Location catalogue status unknown",
    catalogStyle =
      catalogStatus === "demo_reference"
        ? "demo"
        : ["unavailable", "invalid"].includes(catalogStatus)
          ? "bad"
          : catalogStatus
            ? "good"
            : "warn";
  pill("catalog-status", catalogText, catalogStyle);
  const dates = fleet.devices
    .map((device) => Date.parse(device.last_seen_at))
    .filter(Number.isFinite);
  set(
    "last-update",
    dates.length
      ? `Newest device report: ${when(new Date(Math.max(...dates)).toISOString())}`
      : "No live reports yet",
  );
  const workerProblem = !worker.connected;
  $("notice").hidden = !workerProblem && fleet.devices.length > 0;
  set(
    "notice",
    fleet.devices.length === 0
      ? "No devices have reported yet. Start the MQTT worker and firmware, or run the fleet simulator."
      : worker.mode === "demo-direct"
        ? "The direct fleet demo has stopped. Existing values are last known; local device protection is not represented by this simulator."
        : "The MQTT monitoring path is unavailable. Existing values are last known; local ESP32 protection remains independent.",
  );
}
function markFleetLastKnown() {
  fleet.devices = fleet.devices.map((device) => ({
    ...device,
    connection: "unknown",
    display_state: {
      ...device.display_state,
      last_known: true,
      level: device.display_state.level === "normal" ? "unknown" : device.display_state.level,
      title:
        device.display_state.level === "normal"
          ? "LAST-KNOWN NORMAL — NOT CURRENT"
          : device.display_state.title,
    },
  }));
  fleet.stalls = fleet.stalls.map((stall) => ({
    ...stall,
    connection: "unknown",
    temperature_fresh: false,
    state_last_known: true,
    severity: stall.severity === "normal" ? "unknown" : stall.severity,
    state_title:
      stall.severity === "normal" ? "LAST-KNOWN NORMAL — NOT CURRENT" : stall.state_title,
  }));
  fleet.alerts = fleet.alerts.map((alert) => ({
    ...alert,
    connection: "unknown",
    state: { ...alert.state, last_known: true },
  }));
  const summary = {
    total: fleet.stalls.length,
    critical: 0,
    warning: 0,
    fault: 0,
    unknown: 0,
    normal: 0,
    connectivity_issues: fleet.stalls.length,
  };
  for (const stall of fleet.stalls) summary[stall.severity] = (summary[stall.severity] || 0) + 1;
  fleet.summary = summary;
}
async function refresh(forceHistory = false) {
  if (busy) return;
  busy = true;
  try {
    fleet = await fetchFleet();
    updateSystemStatus();
    renderGlobalAlerts();
    renderOverview();
    applyInitialRoute();
    if (activeView === "detail") renderDetail();
    await refreshDetailData(forceHistory);
  } catch (error) {
    document.body.classList.add("transport-lost");
    markFleetLastKnown();
    pill("server-status", "Dashboard data unavailable", "bad");
    pill("worker-status", "MQTT status unknown", "warn");
    pill("catalog-status", "Location status last known", "warn");
    $("notice").hidden = false;
    set(
      "notice",
      "Could not refresh dashboard data. All displayed values are last known; current device status is unknown. Retrying automatically.",
    );
    renderGlobalAlerts();
    renderOverview();
    if (activeView === "detail") renderDetail();
    console.warn("Dashboard refresh failed:", error.message);
  } finally {
    busy = false;
  }
}
for (const id of [
  "stall-search",
  "site-filter",
  "building-filter",
  "zone-filter",
  "severity-filter",
])
  $(id).addEventListener(id === "stall-search" ? "input" : "change", () => {
    const stalls = filteredStalls();
    renderLocationGroups(stalls);
    renderRoster(stalls);
  });
$("clear-filters").addEventListener("click", () => {
  $("stall-search").value = "";
  $("site-filter").value = "";
  $("building-filter").value = "";
  $("zone-filter").value = "";
  $("severity-filter").value = "";
  renderOverview();
});
$("overview-tab").addEventListener("click", () => showView("overview"));
$("back-overview").addEventListener("click", () => showView("overview"));
$("detail-tab").addEventListener("click", () => {
  if (selectedStall) showView("detail");
});
$("brand-home").addEventListener("click", (event) => {
  event.preventDefault();
  showView("overview");
  window.scrollTo({ top: 0, behavior: "smooth" });
});
$("device-select").addEventListener("change", () => {
  selectedDevice = $("device-select").value;
  lastHistoryAt = 0;
  points = [];
  renderEventsMessage("Loading station history…");
  set("chart-caption", "Loading temperature history…");
  renderDetail();
  refreshDetailData(true);
});
$("history-hours").addEventListener("change", () => {
  lastHistoryAt = 0;
  points = [];
  renderEventsMessage("Loading station history…");
  set("chart-caption", "Loading temperature history…");
  drawChart();
  refreshDetailData(true);
});
new ResizeObserver(drawChart).observe($("temperature-chart"));
async function loop() {
  await refresh();
  setTimeout(loop, POLL_MS);
}
loop();
