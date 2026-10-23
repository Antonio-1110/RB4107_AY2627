// Stall history: every unattended-cooking incident of each stall, how far it escalated (unattended,
// buzzer warning, shutdown) and how it ended. Django builds the incidents from the controller's
// telemetry (django/ingest/incidents.py); this view only reads them.

import { fetchStallHistory } from "./api.js";
import { $, make, makeBadge, set } from "./dom.js";
import { locationLine, number, when } from "./format.js";

const PEAK = {
  unattended: ["Unattended", "fault"],
  warning: ["Buzzer warning", "warning"],
  shutdown: ["Shutdown", "critical"],
};

let selected = "",
  data = null,
  loading = false;

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

const date = (value) =>
  value
    ? new Date(value).toLocaleString([], {
        year: "numeric",
        month: "short",
        day: "numeric",
        hour: "2-digit",
        minute: "2-digit",
      })
    : "—";

function renderStalls(stalls) {
  const body = $("hist-stall-rows");
  body.replaceChildren();
  if (!stalls.length) {
    const cell = body.insertRow().insertCell();
    cell.colSpan = 5;
    cell.className = "empty-cell";
    cell.textContent = "No stalls have reported yet.";
    return;
  }
  for (const stall of stalls) {
    const row = body.insertRow();
    row.classList.toggle("selected", stall.stall_id === selected);
    const open = make("button", stall.stall_name, "stall-link");
    open.type = "button";
    open.addEventListener("click", async () => {
      await showStallHistory(stall.stall_id);
      $("hist-detail").scrollIntoView({ behavior: "smooth", block: "start" });
    });
    const cell = row.insertCell();
    cell.append(open, make("small", locationLine(stall.location)));
    row.insertCell().textContent = stall.ongoing
      ? `${stall.incidents} (1 ongoing)`
      : String(stall.incidents);
    row.insertCell().textContent = String(stall.warnings);
    row.insertCell().textContent = String(stall.shutdowns);
    row.insertCell().textContent = stall.last_incident_at ? when(stall.last_incident_at) : "None";
  }
}

function renderIncidents() {
  const stall = data?.stall;
  $("hist-detail").hidden = !stall;
  if (!stall) return;
  set("hist-stall-name", stall.stall_name);
  set("hist-stall-location", locationLine(stall.location));
  const incidents = data.incidents || [];
  set("hist-total", stall.incidents);
  set("hist-unattended", stall.incidents - stall.warnings);
  set("hist-warnings", stall.warnings);
  set("hist-shutdowns", stall.shutdowns);
  set("hist-since", stall.first_incident_at ? date(stall.first_incident_at) : "No incidents");
  $("hist-demo").hidden = !incidents.some((incident) => incident.demo);

  const filter = $("hist-filter").value,
    shown = incidents.filter(
      (incident) =>
        !filter ||
        incident.peak === "shutdown" ||
        (filter === "warning" && incident.peak === "warning"),
    );
  const body = $("hist-rows");
  body.replaceChildren();
  if (!shown.length) {
    const cell = body.insertRow().insertCell();
    cell.colSpan = 7;
    cell.className = "empty-cell";
    cell.textContent = incidents.length
      ? "No incidents match this filter."
      : "No incidents recorded for this stall.";
  }
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
      ? `Showing the newest ${incidents.length} incidents.`
      : `${incidents.length} incident${incidents.length === 1 ? "" : "s"} in total.`,
  );
}

export async function refreshStallHistory() {
  if (loading) return;  // a poll is under way
  loading = true;
  try {
    data = await fetchStallHistory(selected);
    if (!selected && data.stalls.length) {
      // Open the stall with the most incidents first.
      selected = [...data.stalls].sort((a, b) => b.incidents - a.incidents)[0].stall_id;
      data = await fetchStallHistory(selected);
    }
    renderStalls(data.stalls);
    renderIncidents();
  } catch (error) {
    const body = $("hist-stall-rows");
    body.replaceChildren();
    const cell = body.insertRow().insertCell();
    cell.colSpan = 5;
    cell.className = "empty-cell";
    cell.textContent = `Could not load the history (${error.message}).`;
  } finally {
    loading = false;
  }
}

export function showStallHistory(stallId) {
  if (stallId) selected = stallId;
  history.replaceState(null, "", selected ? `#history=${encodeURIComponent(selected)}` : "#history");
  return refreshStallHistory();
}

$("hist-filter").addEventListener("change", renderIncidents);
