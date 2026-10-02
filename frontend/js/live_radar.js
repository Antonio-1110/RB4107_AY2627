"use strict";
// Live radar view: what one presence node's C4002 reports, result by result,
// over the last minute (sensors.<node>.c4002_live, docs/c4002_tuning.md).
// Rows are distance, columns are time. Orange cells are gates the sensor says
// hold a stationary target, blue dots are its moving target. The column on the
// right is how often each gate fired in the window: a gate that fires with the
// room empty is a false positive to turn off or desensitise.

import { fetchC4002Live } from "./api.js";
import { LIVE_POLL_MS } from "./config.js";
import { $ } from "./dom.js";

const WINDOW_MS = 60000;
const HIST_WIDTH = 64;

let deviceId = "",
  node = "",
  settings = null,
  samples = [],
  lastId = 0,
  clockOffset = 0, // client clock minus server clock, from the newest sample
  marks = [], // {at, label} settings changes, client time
  timer = null,
  busy = false,
  error = "";

export function selectLiveNode(nextDevice, nextNode, nextSettings) {
  settings = nextSettings || settings;
  if (nextDevice === deviceId && nextNode === node) return;
  deviceId = nextDevice;
  node = nextNode;
  samples = [];
  lastId = 0;
  marks = [];
  error = "";
  draw();
  schedule(0);
}

// Called when the node confirms new settings, so their effect shows on the timeline.
export function markSettingsChange(label) {
  marks.push({ at: Date.now(), label });
  draw();
}

function schedule(ms) {
  clearTimeout(timer);
  timer = setTimeout(poll, ms);
}

async function poll() {
  if (!deviceId || !node || busy) return;
  if (document.hidden) {
    schedule(LIVE_POLL_MS * 4);
    return;
  }
  busy = true;
  const asked = node;
  try {
    const body = await fetchC4002Live(deviceId, node, lastId);
    if (asked === node && body.samples.length) {
      samples.push(...body.samples);
      lastId = body.last_id;
      clockOffset = Date.now() - Date.parse(body.samples.at(-1).received_at);
    }
    error = "";
  } catch (e) {
    error = `Live data not available: ${e.message}`;
  }
  busy = false;
  const cutoff = Date.now() - clockOffset - WINDOW_MS;
  samples = samples.filter((s) => Date.parse(s.received_at) >= cutoff);
  marks = marks.filter((m) => m.at >= Date.now() - WINDOW_MS);
  draw();
  schedule(LIVE_POLL_MS);
}

const metres = (cm) => `${(cm / 100).toFixed(2)} m`;

function gateRuns(gates, size) {
  // [6,7,8,12] -> "6–8, 12 (1.20–1.80 m, 2.40–2.60 m)"
  const runs = [];
  for (const g of gates) {
    const last = runs.at(-1);
    if (last && g === last[1] + 1) last[1] = g;
    else runs.push([g, g]);
  }
  const ids = runs.map(([a, b]) => (a === b ? `${a}` : `${a}–${b}`)).join(", ");
  const spans = runs.map(([a, b]) => `${((a * size) / 100).toFixed(1)}–${(((b + 1) * size) / 100).toFixed(1)} m`);
  return `${ids} (${spans.join(", ")})`;
}

function renderNow() {
  const now = $("live-now");
  const s = samples.at(-1);
  if (!s) {
    now.textContent = node ? "No live results in the last minute." : "";
    return;
  }
  const parts = [`Sensor says: ${s.target ?? "?"}`];
  if (s.motion.energy > 0 || s.target === "moving") {
    const dir = s.motion.direction && s.motion.direction !== "none" ? `, ${s.motion.direction}` : "";
    parts.push(
      `moving target ${metres(s.motion.distance_cm)}${dir} ${(Math.abs(s.motion.speed_cm_s) / 100).toFixed(2)} m/s, energy ${s.motion.energy}`,
    );
  }
  if (s.presence_gates.length || s.presence.energy > 0) {
    parts.push(
      `stationary target ${metres(s.presence.distance_cm)}, energy ${s.presence.energy}` +
        (s.presence_gates.length ? `, gates ${gateRuns(s.presence_gates, s.gate_size_cm)}` : ""),
    );
  }
  if (s.presence.countdown_s > 0 && s.target !== "none") parts.push(`hold ${s.presence.countdown_s} s`);
  if (s.calibration_remaining_s > 0) parts.push(`calibrating, ${s.calibration_remaining_s} s left`);
  const first = samples[0];
  const span = (Date.parse(s.received_at) - Date.parse(first.received_at)) / 1000;
  if (samples.length > 1 && span > 0) {
    const missed = s.results - first.results + 1 - samples.length;
    parts.push(`${((samples.length - 1) / span).toFixed(1)} results/s${missed > 0 ? `, ${missed} not shown` : ""}`);
  }
  now.textContent = parts.join(" · ");
}

function draw() {
  renderNow();
  const canvas = $("live-canvas");
  $("live-error").textContent = error;
  $("live-error").hidden = !error;
  const rect = canvas.getBoundingClientRect();
  if (rect.width < 50) return;
  const ratio = window.devicePixelRatio || 1;
  canvas.width = rect.width * ratio;
  canvas.height = rect.height * ratio;
  const ctx = canvas.getContext("2d");
  ctx.scale(ratio, ratio);
  const s0 = samples.at(-1);
  const size = s0 ? s0.gate_size_cm : settings?.resolution_cm || 80;
  const gates = size === 20 ? 25 : 15;
  const maxCm = gates * size;
  const left = 46,
    right = rect.width - HIST_WIDTH - 8,
    top = 8,
    bottom = rect.height - 22;
  const end = Date.now() - clockOffset,
    start = end - WINDOW_MS;
  const x = (t) => left + ((t - start) / WINDOW_MS) * (right - left);
  const y = (cm) => bottom - (cm / maxCm) * (bottom - top);
  const rowH = (bottom - top) / gates;
  ctx.font = "10px system-ui";

  // Gate rows; turned-off gates (from the node's settings) shaded grey.
  for (let g = 0; g < gates; g++) {
    const off = settings && settings.resolution_cm === size && settings.presence_gates[g] === 0;
    ctx.fillStyle = off ? "#e3e7ea" : g % 2 ? "#fafcfc" : "#f3f6f7";
    ctx.fillRect(left, y((g + 1) * size), right - left, rowH);
  }
  ctx.fillStyle = "#667783";
  const labelEvery = gates > 15 ? 5 : 3;
  for (let g = 0; g <= gates; g += labelEvery) ctx.fillText(`${((g * size) / 100).toFixed(1)} m`, 4, y(g * size) + 3);
  for (let sec = 60; sec >= 10; sec -= 10) ctx.fillText(`-${sec}s`, x(end - sec * 1000) - 10, bottom + 14);
  ctx.fillText("now", right - 20, bottom + 14);

  // Range limits from the settings.
  if (settings) {
    ctx.strokeStyle = "#137b75";
    ctx.setLineDash([4, 3]);
    for (const cm of [settings.range_min_cm, settings.range_max_cm]) {
      if (cm > 0 && cm < maxCm) {
        ctx.beginPath();
        ctx.moveTo(left, y(cm));
        ctx.lineTo(right, y(cm));
        ctx.stroke();
      }
    }
    ctx.setLineDash([]);
  }

  // Results.
  const hits = new Array(gates).fill(0);
  samples.forEach((s, i) => {
    const t = Date.parse(s.received_at);
    const next = i + 1 < samples.length ? Date.parse(samples[i + 1].received_at) : end;
    const x0 = x(t),
      w = Math.max(1.5, x(Math.min(next, t + 1000)) - x0);
    ctx.fillStyle = "rgba(189, 87, 25, 0.75)";
    for (const g of s.presence_gates) {
      if (g >= gates) continue;
      hits[g]++;
      ctx.fillRect(x0, y((g + 1) * s.gate_size_cm), w, rowH);
    }
    if (s.motion.energy > 0 && s.motion.distance_cm > 0) {
      ctx.fillStyle = "rgba(19, 90, 160, 0.85)";
      ctx.beginPath();
      ctx.arc(x0, y(s.motion.distance_cm), 2 + s.motion.energy / 25, 0, 2 * Math.PI);
      ctx.fill();
    }
  });

  // Settings changes.
  ctx.strokeStyle = "#675493";
  ctx.fillStyle = "#675493";
  for (const m of marks) {
    const mx = x(m.at - clockOffset);
    ctx.beginPath();
    ctx.moveTo(mx, top);
    ctx.lineTo(mx, bottom);
    ctx.stroke();
    ctx.fillText(m.label, mx + 3, top + 10);
  }

  // How often each gate fired in the window.
  const hx = right + 8;
  ctx.fillStyle = "#667783";
  ctx.fillText("% fired", hx + 4, bottom + 14);
  for (let g = 0; g < gates; g++) {
    const share = samples.length ? hits[g] / samples.length : 0;
    ctx.fillStyle = "#f3f6f7";
    ctx.fillRect(hx, y((g + 1) * size) + 1, HIST_WIDTH - 8, rowH - 2);
    ctx.fillStyle = "rgba(189, 87, 25, 0.75)";
    ctx.fillRect(hx, y((g + 1) * size) + 1, (HIST_WIDTH - 8) * share, rowH - 2);
    if (share >= 0.05 && rowH >= 9) {
      ctx.fillStyle = "#142936";
      ctx.fillText(`${g}: ${Math.round(share * 100)}`, hx + 2, y(g * size) - rowH / 2 + 3);
    }
  }
  canvas.setAttribute(
    "aria-label",
    `Radar results for ${node || "no node"} over the last minute: ${samples.length} results`,
  );
}

window.addEventListener("resize", draw);
document.addEventListener("visibilitychange", () => !document.hidden && schedule(0));
