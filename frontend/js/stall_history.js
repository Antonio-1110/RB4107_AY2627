// Stall history: for a fire-safety inspector, how often each stall's unattended-cooking countdown
// started over a chosen period, how many reached the buzzer and the gas shut-off, and how long
// recovery usually took; every stall side by side to pick the one to visit or watch. Django builds
// the incidents from the controller's telemetry and sums them up per period
// (django/ingest/incidents.py); this view only reads them.

import { fetchStallHistory } from "./api.js";
import { drawStackedWeeks } from "./chart.js";
import { $, make, makeBadge, set } from "./dom.js";
import { DATE_LOCALE, locationLine, number } from "./format.js";

const PEAK = {
  unattended: ["Countdown only", "fault"],
  warning: ["Buzzer warning", "warning"],
  shutdown: ["Gas shut-off", "critical"],
};
const PRESETS = { 14: "the last 2 weeks", 28: "the last 4 weeks", 90: "the last 3 months" };
const MAX_DAYS = 366; // the server's limit (MAX_PERIOD_DAYS)
const BARS = [
  { key: "countdown_only", color: "--teal" },
  { key: "buzzer", color: "--amber" },
  { key: "shutoff", color: "--red" },
];

const percent = (share) => (number(share) ? `${Math.round(share * 100)}%` : "—");
const rate = (value) => (number(value) ? value.toFixed(1) : "—");
const plural = (count, word) => `${count} ${word}${count === 1 ? "" : "s"}`;

export function duration(totalSeconds) {
  if (!number(totalSeconds)) return "—";
  const s = Math.round(totalSeconds);
  if (s < 60) return `${s} s`;
  if (s < 3600) return `${Math.floor(s / 60)} min ${s % 60 ? `${s % 60} s` : ""}`.trim();
  const h = Math.floor(s / 3600),
    m = Math.round((s % 3600) / 60);
  return `${h} h${m ? ` ${m} min` : ""}`;
}

// Everything the stalls can be ranked by. For each, a bigger value is worse (more countdowns,
// slower recovery). `value` reads it from a stall's period summary; null means "no data" and
// always sorts last.
const MEASURES = {
  risk: { label: "Highest risk" }, // most shut-offs, then most buzzer warnings
  countdowns_per_week: { label: "Countdowns per week", value: (p) => p.countdowns_per_week, text: rate },
  countdowns_per_cooking_day: {
    label: "Countdowns per cooking day",
    value: (p) => p.countdowns_per_cooking_day,
    text: rate,
  },
  countdowns: { label: "Total countdowns", value: (p) => p.countdowns, text: String },
  buzzer_per_week: { label: "Buzzer warnings per week", value: (p) => p.buzzer_per_week, text: rate },
  buzzer: { label: "Total buzzer warnings", value: (p) => p.buzzer, text: String },
  buzzer_share: { label: "Buzzer warnings as % of countdowns", value: (p) => p.buzzer_share, text: percent },
  shutoff_per_week: { label: "Shut-offs per week", value: (p) => p.shutoff_per_week, text: rate },
  shutoff: { label: "Total shut-offs", value: (p) => p.shutoff, text: String },
  shutoff_share: { label: "Shut-offs as % of buzzer warnings", value: (p) => p.shutoff_share, text: percent },
  usual_recovery: {
    label: "Median recovery",
    value: (p) => p.usual_recovery.median_seconds,
    text: duration,
  },
  slowest_recovery: {
    label: "Slowest recovery",
    value: (p) => p.usual_recovery.slowest_seconds,
    text: duration,
  },
  after_buzzer: {
    label: "Median return time after a buzzer warning",
    value: (p) => p.recovery.after_buzzer.median_seconds,
    text: duration,
  },
  after_shutoff: {
    label: "Median reset time after a shut-off",
    value: (p) => p.recovery.after_shutoff.median_seconds,
    text: duration,
  },
  restarts: {
    label: "Controller restarts",
    value: (p) => p.recovery.controller_restarted.count,
    text: String,
  },
  name: { label: "Stall name" },
};
// Measures with a column of their own; any other ranking adds one more column for itself.
const COLUMNS = new Set(["name", "countdowns_per_week", "buzzer", "shutoff", "usual_recovery"]);
const SHOW = {
  watched: (stall) => watched.has(stall.stall_id),
  shutoff: (stall) => stall.period.shutoff > 0,
  buzzer: (stall) => stall.period.buzzer > 0,
  restart: (stall) => stall.period.recovery.controller_restarted.count > 0,
};

let selected = "",
  period = validPeriod(load("rb4107.historyPeriod", null)),
  watched = new Set(load("rb4107.watchedStalls", [])),
  sortKey = "risk",
  worstFirst = true,
  customOpen = false,
  data = null,
  loading = false,
  again = false; // asked again (new period or stall) while loading

// A period saved by an older page or edited by hand falls back to 4 weeks.
function validPeriod(saved) {
  if (Number.isInteger(saved?.days) && saved.days >= 1 && saved.days <= MAX_DAYS) return { days: saved.days };
  const iso = /^\d{4}-\d{2}-\d{2}$/;
  if (iso.test(saved?.from) && iso.test(saved?.to) && saved.from <= saved.to) return { from: saved.from, to: saved.to };
  return { days: 28 };
}

// Per-viewer conveniences kept in the browser; the page works the same without them.
function load(key, fallback) {
  try {
    return JSON.parse(localStorage.getItem(key)) ?? fallback;
  } catch {
    return fallback;
  }
}
function save(key, value) {
  try {
    localStorage.setItem(key, JSON.stringify(value));
  } catch {
    // private window or blocked storage: just not remembered
  }
}

const isoDay = (d) =>
  `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}`;
const day = (iso) =>
  new Date(`${iso}T00:00:00`).toLocaleDateString(DATE_LOCALE, { year: "numeric", month: "short", day: "numeric" });

const after = (from, to) =>
  from && to ? duration((new Date(to) - new Date(from)) / 1000) : "—";

const date = (value, withTime = true) =>
  value
    ? new Date(value).toLocaleString(DATE_LOCALE, {
        year: "numeric",
        month: "short",
        day: "numeric",
        ...(withTime ? { hour: "2-digit", minute: "2-digit" } : {}),
      })
    : "—";

// "the last 4 weeks", "the last 10 days", "these 6 days": for sentences like "12 in …".
function periodName() {
  if (period.days) return PRESETS[period.days] || `the last ${plural(period.days, "day")}`;
  const from = new Date(`${period.from}T00:00:00`),
    to = new Date(`${period.to}T00:00:00`);
  return `these ${plural(Math.round((to - from) / 86400000) + 1, "day")}`;
}

const byRisk = (a, b) =>
  b.period.shutoff - a.period.shutoff ||
  b.period.buzzer - a.period.buzzer ||
  b.period.countdowns - a.period.countdowns ||
  a.stall_name.localeCompare(b.stall_name);

function compare(a, b) {
  const flip = worstFirst ? 1 : -1;
  if (sortKey === "risk") return flip * byRisk(a, b);
  if (sortKey === "name") return flip * a.stall_name.localeCompare(b.stall_name);
  const value = MEASURES[sortKey].value,
    va = value(a.period),
    vb = value(b.period);
  if (va == null || vb == null) return (va == null) - (vb == null) || byRisk(a, b); // no data: last
  return flip * (vb - va) || byRisk(a, b);
}

const area = (location) => [location.site_name, location.building].filter(Boolean).join(" · ");

function shownStalls() {
  const all = (data?.stalls || []).filter((stall) => stall.period),
    words = $("hist-search").value.trim().toLowerCase(),
    where = $("hist-area").value,
    show = SHOW[$("hist-show").value];
  const shown = all.filter(
    (stall) =>
      (!words ||
        `${stall.stall_name} ${stall.stall_id} ${locationLine(stall.location)}`.toLowerCase().includes(words)) &&
      (!where || area(stall.location) === where) &&
      (!show || show(stall)),
  );
  return { all, shown: shown.sort(compare) };
}

function emptyRow(body, columns, text) {
  const cell = body.insertRow().insertCell();
  cell.colSpan = columns;
  cell.className = "empty-cell";
  cell.textContent = text;
}

function renderPeriod() {
  const preset = period.days && PRESETS[period.days];
  for (const button of document.querySelectorAll(".hist-period-btn[data-days]")) {
    const active = Number(button.dataset.days) === period.days;
    button.classList.toggle("active", active);
    button.setAttribute("aria-pressed", String(active));
  }
  const custom = $("hist-custom-btn");
  custom.classList.toggle("active", !preset);
  custom.textContent = preset ? "Custom…" : `Custom: ${period.days ? plural(period.days, "day") : "dates"}`;
  custom.setAttribute("aria-expanded", String(customOpen));
  $("hist-custom").hidden = !customOpen;
  const today = isoDay(new Date());
  $("hist-from").max = $("hist-to").max = today;
  const length = data?.stalls?.find((stall) => stall.period)?.period.days;
  set(
    "hist-range",
    !data?.since
      ? ""
      : period.from
        ? `${day(period.from)} to ${period.to === today ? "now" : `the end of ${day(period.to)}`}. Counts include countdowns still in progress.`
        : `${date(data.since, false)} to now${length ? ` (${plural(Math.round(length), "day")})` : ""}. Counts include countdowns still in progress.`,
  );
}

function setPeriod(next) {
  period = next;
  save("rb4107.historyPeriod", period);
  set("hist-custom-error", "");
  renderPeriod();
  refreshStallHistory();
}

function renderAreas(all) {
  const select = $("hist-area"),
    current = select.value,
    areas = [...new Set(all.map((stall) => area(stall.location)).filter(Boolean))].sort();
  if (areas.join("|") === [...select.options].slice(1).map((o) => o.value).join("|")) return;
  select.replaceChildren(make("option", "All areas"), ...areas.map((name) => make("option", name)));
  select.options[0].value = "";
  select.value = areas.includes(current) ? current : "";
}

function renderSortHeaders() {
  const extra = !COLUMNS.has(sortKey) && sortKey !== "risk",
    column = $("hist-rank-col");
  column.hidden = !extra;
  column.textContent = extra ? MEASURES[sortKey].label : "";
  column.setAttribute("aria-sort", extra ? (worstFirst ? "descending" : "ascending") : "none");
  for (const th of document.querySelectorAll(".hist-stall-table th")) {
    const key = th.querySelector(".hist-sort")?.dataset.sort;
    if (!key) continue;
    const on = key === sortKey || (sortKey === "risk" && key === "shutoff");
    th.setAttribute(
      "aria-sort",
      !on ? "none" : (key === "name") === worstFirst ? "ascending" : "descending",
    );
  }
  $("hist-rank").value = sortKey;
  const dir = $("hist-rank-dir");
  dir.textContent = sortKey === "name" ? (worstFirst ? "A to Z" : "Z to A") : worstFirst ? "Worst first" : "Best first";
  return extra;
}

function openStall(stallId) {
  showStallHistory(stallId).then(() => {
    // One column (narrow screens): the stall's figures are below the table.
    if (getComputedStyle($("hist-layout")).gridTemplateColumns.split(" ").length < 2)
      $("hist-detail").scrollIntoView({ behavior: "smooth", block: "start" });
  });
}

function renderStalls() {
  const extra = renderSortHeaders(),
    columns = extra ? 7 : 6,
    body = $("hist-stall-rows"),
    { all, shown } = shownStalls();
  renderAreas(all);
  body.replaceChildren();
  set(
    "hist-stall-note",
    all.length
      ? `Showing ${shown.length} of ${plural(all.length, "stall")} · ranked by ${MEASURES[sortKey].label.toLowerCase()}, ${$("hist-rank-dir").textContent.toLowerCase()}.`
      : "",
  );
  if (!all.length) return emptyRow(body, columns, "No stall has reported.");
  if (!shown.length) return emptyRow(body, columns, "No stall matches the filters. Clear the search or select “All stalls”.");
  // Each figure gets a thin bar against the biggest value shown, to compare stalls at a glance.
  const biggest = (key) => Math.max(0, ...shown.map((stall) => MEASURES[key].value(stall.period) ?? 0));
  const scale = Object.fromEntries(
    ["countdowns_per_week", "buzzer", "shutoff", "usual_recovery", ...(extra ? [sortKey] : [])].map((key) => [
      key,
      biggest(key),
    ]),
  );
  for (const stall of shown) {
    const p = stall.period,
      row = body.insertRow(),
      open = () => openStall(stall.stall_id);
    row.className = "hist-stall-row";
    row.classList.toggle("selected", stall.stall_id === selected);
    row.tabIndex = 0;
    row.setAttribute("aria-label", `Open ${stall.stall_name}`);
    row.addEventListener("click", open);
    row.addEventListener("keydown", (event) => {
      if (event.target === row && (event.key === "Enter" || event.key === " ")) {
        event.preventDefault();
        open();
      }
    });

    const starred = watched.has(stall.stall_id),
      star = make("button", starred ? "★" : "☆", `hist-star${starred ? " on" : ""}`);
    star.type = "button";
    star.title = starred ? "Starred. Click to remove the star." : "Star this stall to track it";
    star.setAttribute("aria-pressed", String(starred));
    star.setAttribute("aria-label", `Star ${stall.stall_name}`);
    star.addEventListener("click", (event) => {
      event.stopPropagation();
      if (watched.has(stall.stall_id)) watched.delete(stall.stall_id);
      else watched.add(stall.stall_id);
      save("rb4107.watchedStalls", [...watched]);
      renderStalls();
    });
    const starCell = row.insertCell();
    starCell.className = "hist-star-col";
    starCell.append(star);

    const name = row.insertCell();
    name.className = "hist-name";
    name.title = locationLine(stall.location);
    name.append(
      make("strong", stall.stall_name),
      make("small", [stall.location.building, stall.location.level, stall.location.area].filter(Boolean).join(" · ")),
    );

    const figure = (key, main, note, style = "") => {
      const td = row.insertCell(),
        bar = make("span", null, "hist-bar"),
        fill = make("span");
      td.className = `num ${style}`.trim();
      const value = MEASURES[key].value(p);
      fill.style.width = `${scale[key] && value ? Math.max(3, (value / scale[key]) * 100) : 0}%`;
      bar.append(fill);
      td.append(make("strong", main), make("small", note), bar);
    };
    figure("countdowns_per_week", rate(p.countdowns_per_week), `${p.countdowns} total`, "hist-count");
    figure(
      "buzzer",
      String(p.buzzer),
      p.countdowns ? `${percent(p.buzzer_share)} of countdowns` : "",
      `hist-buzz${p.buzzer ? " hist-warn" : ""}`,
    );
    figure(
      "shutoff",
      String(p.shutoff),
      p.buzzer ? `${percent(p.shutoff_share)} of warnings` : "",
      `hist-cut${p.shutoff ? " hist-crit" : ""}`,
    );
    figure(
      "usual_recovery",
      duration(p.usual_recovery.median_seconds),
      p.usual_recovery.count ? `slowest ${duration(p.usual_recovery.slowest_seconds)}` : "",
      "hist-time",
    );
    if (extra) figure(sortKey, MEASURES[sortKey].text(MEASURES[sortKey].value(p)), "", "hist-rank-cell");
  }
}

function renderFigures(p) {
  set("hist-per-week", `${rate(p.countdowns_per_week)} / week`);
  const cooking = p.cooking_days
    ? `${rate(p.countdowns_per_cooking_day)} per cooking day (${plural(p.cooking_days, "cooking day")} recorded)`
    : "per cooking day: no cooking recorded";
  set("hist-per-week-note", `${p.countdowns} in ${periodName()} · ${cooking}`);
  set("hist-buzzer", String(p.buzzer));
  set("hist-buzzer-note", p.countdowns ? `${percent(p.buzzer_share)} of the countdowns` : "No countdowns");
  set("hist-shutoff", String(p.shutoff));
  set("hist-shutoff-note", p.buzzer ? `${percent(p.shutoff_share)} of the buzzer warnings` : "No buzzer warnings");
  set("hist-recovery", duration(p.usual_recovery.median_seconds));
  set(
    "hist-recovery-note",
    p.usual_recovery.count
      ? `Median from countdown start to return to normal · slowest ${duration(p.usual_recovery.slowest_seconds)}`
      : "No completed recoveries in this period",
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
      name.append(make("small", "May indicate that the controller was power-cycled to bypass the countdown."));
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
    row.insertCell().textContent = "In progress";
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
      incidents.length ? "No countdowns match this filter." : "No countdowns at this stall in this period.",
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
        ? makeBadge("In progress", style)
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
      : `${plural(incidents.length, "countdown")} in ${periodName()}.`,
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
    data = await fetchStallHistory(selected, period);
    if (!data.stall) {
      // Nothing (or an unknown stall) chosen: open the riskiest stall of the period.
      const first = data.stalls.filter((stall) => stall.period).sort(byRisk)[0];
      if (first) {
        selected = first.stall_id;
        data = await fetchStallHistory(selected, period);
      }
    }
    renderPeriod();
    renderStalls();
    renderDetail();
  } catch (error) {
    const body = $("hist-stall-rows");
    body.replaceChildren();
    emptyRow(body, 7, `Could not load stall history (${error.message}).`);
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

for (const button of document.querySelectorAll(".hist-period-btn[data-days]")) {
  button.addEventListener("click", () => {
    customOpen = false;
    setPeriod({ days: Number(button.dataset.days) });
  });
}
$("hist-custom-btn").addEventListener("click", () => {
  customOpen = !customOpen;
  if (customOpen) {
    // Start from the period on screen.
    const length = data?.stalls?.find((stall) => stall.period)?.period.days;
    $("hist-days").value = period.days || Math.round(length || 28);
    $("hist-to").value = period.to || isoDay(new Date());
    $("hist-from").value = period.from || isoDay(new Date(Date.now() - ((period.days || 28) - 1) * 86400000));
  }
  renderPeriod();
});
$("hist-days-form").addEventListener("submit", (event) => {
  event.preventDefault();
  const days = Number($("hist-days").value);
  if (!Number.isInteger(days) || days < 1 || days > MAX_DAYS)
    return set("hist-custom-error", `Enter a whole number of days from 1 to ${MAX_DAYS}.`);
  setPeriod({ days });
});
$("hist-dates-form").addEventListener("submit", (event) => {
  event.preventDefault();
  const from = $("hist-from").value,
    to = $("hist-to").value,
    span = (new Date(`${to}T00:00:00`) - new Date(`${from}T00:00:00`)) / 86400000 + 1;
  if (!from || !to) return set("hist-custom-error", "Select both dates.");
  if (span < 1) return set("hist-custom-error", "The start date must be on or before the end date.");
  if (to > isoDay(new Date())) return set("hist-custom-error", "The period cannot end after today.");
  if (span > MAX_DAYS) return set("hist-custom-error", `The period can be at most ${MAX_DAYS} days.`);
  setPeriod({ from, to });
});
for (const button of document.querySelectorAll(".hist-sort")) {
  button.addEventListener("click", () => {
    const key = button.dataset.sort;
    if (key === sortKey || (key === "shutoff" && sortKey === "risk")) worstFirst = !worstFirst;
    else [sortKey, worstFirst] = [key, true];
    renderStalls();
  });
}
$("hist-rank").replaceChildren(
  ...Object.entries(MEASURES).map(([key, { label }]) => {
    const option = make("option", label);
    option.value = key;
    return option;
  }),
);
$("hist-rank").addEventListener("change", (event) => {
  [sortKey, worstFirst] = [event.target.value, true];
  renderStalls();
});
$("hist-rank-dir").addEventListener("click", () => {
  worstFirst = !worstFirst;
  renderStalls();
});
for (const id of ["hist-search", "hist-area", "hist-show"]) $(id).addEventListener("input", renderStalls);
$("hist-filter").addEventListener("change", renderIncidents);
window.addEventListener("resize", renderChart);
