// Stall history: for a fire-safety inspector, how often each stall's unattended-cooking countdown
// started over a chosen period, how many reached the buzzer and the gas shut-off, and how long
// recovery usually took. Django builds the incidents from the controller's telemetry and sums
// them up per period (django/ingest/incidents.py); this view only reads them.

import { fetchStallHistory } from "./api.js";
import { drawStackedWeeks } from "./chart.js";
import { $, make, makeBadge, set } from "./dom.js";
import { locationLine, number } from "./format.js";

const PEAK = {
  unattended: ["Countdown only", "fault"],
  warning: ["Buzzer", "warning"],
  shutdown: ["Shut-off", "critical"],
};
const PERIODS = { 14: "2 weeks", 28: "4 weeks", 90: "3 months" };
const BARS = [
  { key: "countdown_only", color: "--teal" },
  { key: "buzzer", color: "--amber" },
  { key: "shutoff", color: "--red" },
];
// Column sorts, biggest (riskiest) first; ties fall back to the default order.
const SORTS = {
  name: (a, b) => a.stall_name.localeCompare(b.stall_name),
  countdowns: (a, b) => b.period.countdowns - a.period.countdowns,
  buzzer: (a, b) => b.period.buzzer - a.period.buzzer,
  shutoff: (a, b) => b.period.shutoff - a.period.shutoff || b.period.buzzer - a.period.buzzer,
  recovery: (a, b) =>
    (b.period.usual_recovery.median_seconds ?? -1) - (a.period.usual_recovery.median_seconds ?? -1),
};
const DEFAULT_SORT = "shutoff";

let selected = "",
  days = savedDays(),
  sortKey = DEFAULT_SORT,
  data = null,
  loading = false,
  again = false; // asked again (new period or stall) while loading

function savedDays() {
  try {
    const value = Number(localStorage.getItem("rb4107.historyDays"));
    return PERIODS[value] ? value : 28;
  } catch {
    return 28;
  }
}

export function duration(totalSeconds) {
  if (!number(totalSeconds)) return "—";
  const s = Math.round(totalSeconds);
  if (s < 60) return `${s} s`;
  if (s < 3600) return `${Math.floor(s / 60)} min ${s % 60 ? `${s % 60} s` : ""}`.trim();
  const h = Math.floor(s / 3600),
    m = Math.round((s % 3600) / 60);
  return `${h} h${m ? ` ${m} min` : ""}`;
}

const after = (from, to) =>
  from && to ? duration((new Date(to) - new Date(from)) / 1000) : "—";

const date = (value, withTime = true) =>
  value
    ? new Date(value).toLocaleString([], {
        year: "numeric",
        month: "short",
        day: "numeric",
        ...(withTime ? { hour: "2-digit", minute: "2-digit" } : {}),
      })
    : "—";

const percent = (share) => (number(share) ? `${Math.round(share * 100)}%` : "—");
const rate = (value) => (number(value) ? value.toFixed(1) : "—");
const plural = (count, word) => `${count} ${word}${count === 1 ? "" : "s"}`;

const byRisk = (a, b) =>
  SORTS.shutoff(a, b) || SORTS.countdowns(a, b) || SORTS.name(a, b);

function sortedStalls() {
  const stalls = (data?.stalls || []).filter((stall) => stall.period);
  return stalls.sort((a, b) => SORTS[sortKey](a, b) || byRisk(a, b));
}

function emptyRow(body, columns, text) {
  const cell = body.insertRow().insertCell();
  cell.colSpan = columns;
  cell.className = "empty-cell";
  cell.textContent = text;
}

function renderPeriod() {
  for (const button of document.querySelectorAll(".hist-period-btn")) {
    const active = Number(button.dataset.days) === days;
    button.classList.toggle("active", active);
    button.setAttribute("aria-pressed", String(active));
  }
  set(
    "hist-range",
    data?.since
      ? `${date(data.since, false)} to today. Counts include countdowns still going on.`
      : "",
  );
}

function renderStalls() {
  for (const th of document.querySelectorAll(".hist-stall-table th")) {
    const key = th.querySelector(".hist-sort")?.dataset.sort;
    th.setAttribute("aria-sort", key === sortKey ? (key === "name" ? "ascending" : "descending") : "none");
  }
  const body = $("hist-stall-rows");
  body.replaceChildren();
  const stalls = sortedStalls();
  if (!stalls.length) return emptyRow(body, 5, "No stalls have reported yet.");
  for (const stall of stalls) {
    const p = stall.period,
      row = body.insertRow();
    row.classList.toggle("selected", stall.stall_id === selected);
    const open = make("button", stall.stall_name, "stall-link");
    open.type = "button";
    open.addEventListener("click", async () => {
      await showStallHistory(stall.stall_id);
      // One column (narrow screens): the stall's figures are below the table.
      if (getComputedStyle($("hist-layout")).gridTemplateColumns.split(" ").length < 2)
        $("hist-detail").scrollIntoView({ behavior: "smooth", block: "start" });
    });
    const cell = row.insertCell();
    cell.append(open, make("small", locationLine(stall.location)));
    const figure = (main, note, style = "") => {
      const td = row.insertCell();
      td.className = `num ${style}`.trim();
      td.append(make("strong", main), make("small", note));
    };
    figure(rate(p.countdowns_per_week), `${p.countdowns} in total`);
    figure(String(p.buzzer), p.countdowns ? `${percent(p.buzzer_share)} of countdowns` : "", p.buzzer ? "hist-warn" : "");
    figure(String(p.shutoff), p.buzzer ? `${percent(p.shutoff_share)} of buzzers` : "", p.shutoff ? "hist-crit" : "");
    figure(
      duration(p.usual_recovery.median_seconds),
      p.usual_recovery.count ? `slowest ${duration(p.usual_recovery.slowest_seconds)}` : "",
    );
  }
}

function renderFigures(p) {
  set("hist-per-week", `${rate(p.countdowns_per_week)} / week`);
  const cooking = p.cooking_days
    ? `${rate(p.countdowns_per_cooking_day)} per cooking day (${plural(p.cooking_days, "cooking day")} on record)`
    : "per cooking day: no cooking recorded";
  set("hist-per-week-note", `${p.countdowns} in ${PERIODS[days]} · ${cooking}`);
  set("hist-buzzer", String(p.buzzer));
  set("hist-buzzer-note", p.countdowns ? `${percent(p.buzzer_share)} of the countdowns` : "No countdowns");
  set("hist-shutoff", String(p.shutoff));
  set("hist-shutoff-note", p.buzzer ? `${percent(p.shutoff_share)} of the buzzer warnings` : "No buzzer warnings");
  set("hist-recovery", duration(p.usual_recovery.median_seconds));
  set(
    "hist-recovery-note",
    p.usual_recovery.count
      ? `Middle value, countdown start to back to normal · slowest ${duration(p.usual_recovery.slowest_seconds)}`
      : "Nothing recovered in this period",
  );
}

function renderRecovery(p) {
  const body = $("hist-recovery-rows");
  body.replaceChildren();
  for (const [key, group] of Object.entries(p.recovery)) {
    const row = body.insertRow();
    const name = row.insertCell();
    name.textContent = group.label;
    if (key === "controller_restarted" && group.count) {
      row.className = "hist-restart";
      name.append(make("small", "Can mean someone switched the controller off and on to get round it."));
    }
    for (const text of [
      String(group.count),
      duration(group.median_seconds),
      duration(group.slowest_seconds),
    ]) {
      const td = row.insertCell();
      td.className = "num";
      td.textContent = text;
    }
  }
  if (p.ongoing) {
    const row = body.insertRow();
    row.insertCell().textContent = "Still going on";
    for (const text of [String(p.ongoing), "—", "—"]) {
      const td = row.insertCell();
      td.className = "num";
      td.textContent = text;
    }
  }
}

function renderChart() {
  const p = data?.stall?.period;
  if (!p || $("hist-detail").hidden) return;
  drawStackedWeeks($("hist-week-chart"), $("hist-week-empty"), p.weeks, BARS);
}

function renderIncidents() {
  const incidents = data.incidents || [];
  const filter = $("hist-filter").value,
    shown = incidents.filter(
      (incident) =>
        !filter ||
        incident.peak === "shutdown" ||
        (filter === "warning" && incident.peak === "warning"),
    );
  const body = $("hist-rows");
  body.replaceChildren();
  if (!shown.length)
    emptyRow(
      body,
      7,
      incidents.length ? "No incidents match this filter." : "No countdowns at this stall in this period.",
    );
  for (const incident of shown) {
    const row = body.insertRow();
    row.className = `hist-row peak-${incident.peak}`;
    const started = row.insertCell();
    started.textContent = date(incident.started_at);
    if (incident.demo) started.append(" ", make("span", "demo", "hist-demo-tag"));
    const [label, style] = PEAK[incident.peak] || [incident.peak, "unknown"];
    row.insertCell().append(makeBadge(label, style));
    row.insertCell().textContent = after(incident.started_at, incident.warning_at);
    row.insertCell().textContent = after(incident.started_at, incident.shutdown_at);
    row.insertCell().textContent = `${duration(incident.duration_seconds)}${incident.ongoing ? " so far" : ""}`;
    row.insertCell().append(
      incident.ongoing
        ? makeBadge("Ongoing", style)
        : document.createTextNode(incident.end_reason_text),
    );
    row.insertCell().textContent = number(incident.max_temp_c)
      ? `${incident.max_temp_c.toFixed(0)} °C`
      : "—";
  }
  set(
    "hist-note",
    data.truncated
      ? `Showing the newest ${incidents.length} countdowns of the period.`
      : `${plural(incidents.length, "countdown")} in the last ${PERIODS[days]}.`,
  );
}

function renderDetail() {
  const stall = data?.stall;
  $("hist-detail").hidden = !stall?.period;
  if (!stall?.period) return;
  set("hist-stall-name", stall.stall_name);
  set("hist-stall-location", locationLine(stall.location));
  $("hist-demo").hidden = !(data.incidents || []).some((incident) => incident.demo);
  renderFigures(stall.period);
  renderRecovery(stall.period);
  renderChart();
  renderIncidents();
}

export async function refreshStallHistory() {
  if (loading) {
    again = true;  // a poll is under way: run once more when it is done
    return;
  }
  loading = true;
  try {
    data = await fetchStallHistory(selected, days);
    if (!data.stall) {
      // Nothing (or an unknown stall) chosen: open the riskiest stall of the period.
      const first = sortedStalls().sort(byRisk)[0];
      if (first) {
        selected = first.stall_id;
        data = await fetchStallHistory(selected, days);
      }
    }
    renderPeriod();
    renderStalls();
    renderDetail();
  } catch (error) {
    const body = $("hist-stall-rows");
    body.replaceChildren();
    emptyRow(body, 5, `Could not load the history (${error.message}).`);
  } finally {
    loading = false;
    if (again) {
      again = false;
      refreshStallHistory();
    }
  }
}

export function showStallHistory(stallId) {
  if (stallId) selected = stallId;
  history.replaceState(null, "", selected ? `#history=${encodeURIComponent(selected)}` : "#history");
  return refreshStallHistory();
}

for (const button of document.querySelectorAll(".hist-period-btn")) {
  button.addEventListener("click", () => {
    days = Number(button.dataset.days);
    try {
      localStorage.setItem("rb4107.historyDays", String(days));
    } catch {
      // private window or blocked storage: the period just isn't remembered
    }
    renderPeriod();
    refreshStallHistory();
  });
}
for (const button of document.querySelectorAll(".hist-sort")) {
  button.addEventListener("click", () => {
    sortKey = sortKey === button.dataset.sort ? DEFAULT_SORT : button.dataset.sort;
    renderStalls();
  });
}
$("hist-filter").addEventListener("change", renderIncidents);
window.addEventListener("resize", renderChart);
