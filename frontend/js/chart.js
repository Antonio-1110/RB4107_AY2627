// Temperature history graph, drawn on a <canvas> without a chart library.

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
    ctx.strokeStyle = "#e9eef0";
    ctx.beginPath();
    ctx.moveTo(left, py);
    ctx.lineTo(right, py);
    ctx.stroke();
    ctx.fillStyle = "#74858d";
    ctx.textAlign = "right";
    ctx.fillText((max - ((max - min) * i) / 4).toFixed(0), left - 9, py + 3);
  }
  ctx.strokeStyle = "#18867e";
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
  ctx.fillStyle = "#18867e";
  ctx.beginPath();
  ctx.arc(x(last), y(last), 3, 0, Math.PI * 2);
  ctx.fill();
  ctx.fillStyle = "#74858d";
  ctx.textAlign = "left";
  ctx.fillText(when(points[0].received_at), left, height - 5);
  ctx.textAlign = "right";
  ctx.fillText(when(last.received_at), right, height - 5);
  canvas.setAttribute(
    "aria-label",
    `${points.length} temperature readings. Latest ${last.temperature_c} degrees Celsius.`,
  );
}
