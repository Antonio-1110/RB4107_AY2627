"use strict";
// Logs tab: the lines the controller prints on its serial port, sent over MQTT
// (controller/log, docs/logging.md), and its per-tag log levels. Polls only
// while the Logs tab is open in Tuning mode. Changing a level only changes what
// the controller logs, never what it does.

import { fetchLogs, sendLogLevel } from "./api.js";
import { getTab, isTuning, onModeChange } from "./mode.js";
import { $, make, set } from "./dom.js";
import { when } from "./format.js";

const POLL_MS = 1000;
const FIRST_LINES = 300;
const MAX_LINES = 1500; // kept in the page; older ones scroll away
const LEVELS = ["none", "error", "warn", "info", "debug", "verbose"];
const LEVEL_NAME = { E: "Error", W: "Warning", I: "Info", D: "Debug", V: "Verbose" };

let deviceId = "",
  lines = [],
  lastId = 0,
  generation = 0, // bumped when the device or a filter changes: late answers are ignored
  timer = null,
  busy = false,
  levels = {},
  knownTags = [];

const open = () => isTuning() && getTab() === "logs" && !document.hidden && !!deviceId;

function filters() {
  return { level: $("logs-level").value, tag: $("logs-tag").value, q: $("logs-search").value.trim() };
}

export function renderLogs(device) {
  if (!device || device.device_id === deviceId) return;
  deviceId = device.device_id;
  restart();
}

function restart() {
  generation++;
  lines = [];
  lastId = 0;
  draw();
  schedule(0);
}

function schedule(ms) {
  clearTimeout(timer);
  timer = setTimeout(poll, ms);
}

async function poll() {
  if (!open()) {
    schedule(POLL_MS * 3);
    return;
  }
  if (busy) return;
  busy = true;
  const asked = generation;
  try {
    const limit = lastId ? 1000 : FIRST_LINES;
    const body = await fetchLogs(deviceId, { ...filters(), after: lastId, limit });
    if (asked === generation) {
      const first = lastId === 0;
      if (body.lines.length) lines.push(...body.lines);
      if (lines.length > MAX_LINES) lines = lines.slice(-MAX_LINES);
      // Lines filtered out still move the cursor on, so they are not asked for again; a full
      // page continues from its last line instead.
      const newest = body.lines.at(-1)?.id ?? 0;
      lastId = Math.max(lastId, body.lines.length === limit && !first ? newest : Math.max(body.last_id, newest));
      levels = body.levels || {};
      knownTags = body.tags || [];
      status(body);
      if (first || body.lines.length) draw(true);
      drawLevels();
    }
  } catch (e) {
    if (asked === generation) set("logs-status", `Log not available: ${e.message}`);
  }
  busy = false;
  schedule(POLL_MS);
}

function status(body) {
  const age = body.levels_age_seconds;
  const parts = [
    age == null
      ? "No log message from the controller yet: it needs firmware with RB_MQTT_PUBLISH_LOG (on by default)."
      : age > 45
        ? `Last log message ${Math.round(age)} s ago: controller offline or broker unreachable.`
        : "Live.",
    body.dropped ? `${body.dropped} lines lost on the controller since it booted (buffer full).` : "",
    `Lines are kept ${body.retention_days} days.`,
  ];
  set("logs-status", parts.filter(Boolean).join(" "));
}

function lineRow(line) {
  const row = make("div", null, `log-line lvl-${line.level}`);
  const time = make("span", when(line.received_at), "log-time");
  time.title = `received ${new Date(line.received_at).toLocaleString()}`;
  const uptime = make("span", `${(line.uptime_ms / 1000).toFixed(1)} s`, "log-uptime");
  uptime.title = "controller uptime when it logged the line";
  const level = make("span", line.level, "log-level");
  level.title = LEVEL_NAME[line.level] || line.level;
  const tag = make("span", line.tag, "log-tag");
  tag.title = line.tag;
  row.append(time, uptime, level, tag, make("span", line.msg, "log-msg"));
  return row;
}

function draw(added = false) {
  const view = $("logs-view");
  const atBottom = view.scrollHeight - view.scrollTop - view.clientHeight < 40;
  set("logs-count", `${lines.length} line${lines.length === 1 ? "" : "s"}`);
  if (!lines.length) {
    view.replaceChildren(make("p", deviceId ? "No log lines match." : "No controller selected.", "empty-cell"));
    return;
  }
  const rows = [];
  let boot = null;
  for (const line of lines) {
    if (boot !== null && line.boot_id !== boot)
      rows.push(make("div", `Controller restarted (boot ${line.boot_id || "unknown"})`, "log-boot"));
    boot = line.boot_id;
    rows.push(lineRow(line));
  }
  view.replaceChildren(...rows);
  // Follow: new lines scroll into view unless you scrolled up to read older ones.
  if ($("logs-follow").checked && (!added || atBottom || lines.length <= FIRST_LINES))
    view.scrollTop = view.scrollHeight;
}

function levelSelect(tag, level) {
  const select = make("select");
  select.setAttribute("aria-label", `Log level of ${tag === "*" ? "every other tag" : tag}`);
  for (const name of LEVELS) {
    const option = make("option", name);
    option.value = name;
    select.append(option);
  }
  select.value = level;
  select.addEventListener("change", () => send({ tag, level: select.value }));
  return select;
}

function drawLevels() {
  const body = $("logs-levels-rows");
  const entries = Object.entries(levels);
  if (!entries.length) {
    const cell = make("td", "Not reported yet", "empty-cell");
    cell.colSpan = 2;
    body.replaceChildren(make("tr"));
    body.firstChild.append(cell);
  } else if (!body.contains(document.activeElement)) {
    // "*" first, then the tags by name. Not redrawn while a select is in use.
    entries.sort(([a], [b]) => (a === "*" ? -1 : b === "*" ? 1 : a.localeCompare(b)));
    body.replaceChildren(
      ...entries.map(([tag, level]) => {
        const row = make("tr");
        row.append(make("td", tag === "*" ? "Every other tag (*)" : tag), make("td"));
        row.lastChild.append(levelSelect(tag, level));
        return row;
      }),
    );
  }
  const tagSelect = $("logs-tag"),
    chosen = tagSelect.value,
    have = [...tagSelect.options].map((o) => o.value).slice(1);
  if (knownTags.join() !== have.join()) {
    tagSelect.replaceChildren(make("option", "All tags"), ...knownTags.map((t) => make("option", t)));
    tagSelect.firstChild.value = "";
    tagSelect.value = knownTags.includes(chosen) ? chosen : "";
    $("logs-tag-list").replaceChildren(...knownTags.map((t) => make("option", t)));
  }
}

async function send(body) {
  const note = $("logs-level-status");
  note.hidden = false;
  note.className = "tuning-status muted";
  note.textContent = "Sending…";
  try {
    await sendLogLevel(deviceId, body);
    note.className = "tuning-status good";
    note.textContent = body.defaults
      ? "Sent: back to the menuconfig levels. The table updates when the controller confirms."
      : `Sent: ${body.tag} → ${body.level}. The table updates when the controller confirms.`;
  } catch (e) {
    note.className = "tuning-status bad";
    note.textContent = `Not sent: ${e.message}`;
  }
}

$("logs-set-level").addEventListener("click", () => {
  const tag = $("logs-new-tag").value.trim();
  if (!/^(\*|[A-Za-z0-9_.-]{1,15})$/.test(tag)) {
    const note = $("logs-level-status");
    note.hidden = false;
    note.className = "tuning-status bad";
    note.textContent = 'Type a tag: "*" or up to 15 letters, digits, _ - . (e.g. SAFETY).';
    return;
  }
  send({ tag, level: $("logs-new-level").value });
});
$("logs-defaults").addEventListener("click", () => send({ defaults: true }));
for (const id of ["logs-level", "logs-tag"]) $(id).addEventListener("change", restart);
let searchTimer = null;
$("logs-search").addEventListener("input", () => {
  clearTimeout(searchTimer);
  searchTimer = setTimeout(restart, 300);
});
$("logs-follow").addEventListener("change", () => draw());
onModeChange(() => schedule(0));
document.addEventListener("visibilitychange", () => schedule(0));
