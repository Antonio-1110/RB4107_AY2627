// Temperature history graph, drawn on a <canvas> without a chart library.

import { cssVar } from "./dom.js";
import { when } from "./format.js";

// Draws `points` ({received_at, temperature_c}) onto `canvas`. Readings more
// than `gapSeconds` apart are not joined, so outages show as gaps.
export function drawTemperatureChart(canvas, emptyMessage, points, gapSeconds) {
  const rect = canvas.getBoundingClientRect();
  if (rect.width < 50) return;
  const ratio = window.devicePixelRatio || 1;
  canvas.width = rect.width * ratio;
  canvas.height = rect.height * ratio;
  const ctx = canvas.getContext("2d");
  ctx.scale(ratio, ratio);
  const width = rect.width,
    height = rect.height,
    left = 42,
    right = width - 12,
    top = 15,
    bottom = height - 28;
  emptyMessage.hidden = points.length > 0;
  if (!points.length) {
    canvas.setAttribute("aria-label", "No temperature readings in selected window");
    return;
  }
  const temperatures = points.map((point) => point.temperature_c),
    min = Math.floor((Math.min(...temperatures) - 5) / 10) * 10,
    max = Math.ceil((Math.max(...temperatures) + 5) / 10) * 10,
    start = Date.parse(points[0].received_at),
    end = Math.max(start + 1000, Date.parse(points.at(-1).received_at)),
    x = (point) =>
      left + ((Date.parse(point.received_at) - start) / (end - start)) * (right - left),
    y = (point) => bottom - ((point.temperature_c - min) / (max - min)) * (bottom - top);
  ctx.font = "10px system-ui";
  ctx.lineWidth = 1;
  for (let i = 0; i <= 4; i++) {
    const py = top + ((bottom - top) * i) / 4;
    ctx.strokeStyle = cssVar("--line");
    ctx.beginPath();
    ctx.moveTo(left, py);
    ctx.lineTo(right, py);
    ctx.stroke();
    ctx.fillStyle = cssVar("--muted");
    ctx.textAlign = "right";
    ctx.fillText((max - ((max - min) * i) / 4).toFixed(0), left - 9, py + 3);
  }
  ctx.strokeStyle = cssVar("--teal");
  ctx.lineWidth = 2;
  ctx.beginPath();
  points.forEach((point, index) => {
    if (
      index === 0 ||
      Date.parse(point.received_at) - Date.parse(points[index - 1].received_at) > gapSeconds * 1000
    )
      ctx.moveTo(x(point), y(point));
    else ctx.lineTo(x(point), y(point));
  });
  ctx.stroke();
  const last = points.at(-1);
  ctx.fillStyle = cssVar("--teal");
  ctx.beginPath();
  ctx.arc(x(last), y(last), 3, 0, Math.PI * 2);
  ctx.fill();
  ctx.fillStyle = cssVar("--muted");
  ctx.textAlign = "left";
  ctx.fillText(when(points[0].received_at), left, height - 5);
  ctx.textAlign = "right";
  ctx.fillText(when(last.received_at), right, height - 5);
  canvas.setAttribute(
    "aria-label",
    `${points.length} temperature readings. Latest ${last.temperature_c} degrees Celsius.`,
  );
}

// Stacked bar chart, one bar per week, drawn the same way. `weeks` are the API's
// period weeks ({week_start, days_in_period, ...counts}); `series` is a list of
// {key, color} stacked bottom to top. A week only partly inside the period gets
// a lighter label ("part").
export function drawStackedWeeks(canvas, emptyMessage, weeks, series) {
  const rect = canvas.getBoundingClientRect();
  if (rect.width < 50) return;
  const ratio = window.devicePixelRatio || 1;
  canvas.width = rect.width * ratio;
  canvas.height = rect.height * ratio;
  const ctx = canvas.getContext("2d");
  ctx.scale(ratio, ratio);
  const total = (week) => series.reduce((sum, { key }) => sum + week[key], 0),
    most = Math.max(0, ...weeks.map(total));
  emptyMessage.hidden = most > 0;
  if (!most) {
    canvas.setAttribute("aria-label", "No countdowns in this period");
    return;
  }
  const width = rect.width,
    height = rect.height,
    left = 34,
    right = width - 8,
    top = 12,
    bottom = height - 30,
    step = Math.max(1, Math.ceil(most / 4)),
    max = step * Math.ceil(most / step),
    y = (value) => bottom - (value / max) * (bottom - top),
    slot = (right - left) / weeks.length,
    bar = Math.min(48, slot * 0.68);
  ctx.font = "10px system-ui";
  ctx.lineWidth = 1;
  for (let value = 0; value <= max; value += step) {
    ctx.strokeStyle = cssVar("--line");
    ctx.beginPath();
    ctx.moveTo(left, y(value));
    ctx.lineTo(right, y(value));
    ctx.stroke();
    ctx.fillStyle = cssVar("--muted");
    ctx.textAlign = "right";
    ctx.fillText(String(value), left - 8, y(value) + 3);
  }
  const labelEvery = Math.ceil(weeks.length / Math.max(1, Math.floor((right - left) / 46)));
  weeks.forEach((week, index) => {
    const x = left + slot * index + (slot - bar) / 2;
    let base = 0;
    for (const { key, color } of series) {
      if (!week[key]) continue;
      ctx.fillStyle = cssVar(color);
      // 1 px gap between segments keeps adjacent colours apart.
      ctx.fillRect(x, y(base + week[key]), bar, y(base) - y(base + week[key]) - (base ? 1 : 0));
      base += week[key];
    }
    if (base) {
      ctx.fillStyle = cssVar("--ink");
      ctx.textAlign = "center";
      ctx.fillText(String(base), x + bar / 2, y(base) - 4);
    }
    if (index % labelEvery) return;
    const start = new Date(`${week.week_start}T00:00:00`);
    ctx.fillStyle = cssVar("--muted");
    ctx.textAlign = "center";
    ctx.fillText(start.toLocaleDateString([], { day: "numeric", month: "short" }), x + bar / 2, bottom + 14);
    if (week.days_in_period < 7) ctx.fillText("(part)", x + bar / 2, bottom + 26);
  });
  canvas.setAttribute(
    "aria-label",
    `Countdowns per week: ${weeks.map((week) => `week of ${week.week_start}, ${total(week)}`).join("; ")}.`,
  );
}
