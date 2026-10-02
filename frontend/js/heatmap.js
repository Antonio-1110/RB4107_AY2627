// MLX90640 heat map: decodes one picture from /thermal_frame/ and draws it on a
// <canvas>. Display only; the safety decision never uses these pictures.

// Sequential warm ramp, light (cool) -> dark (hot). One hue family, no rainbow.
const RAMP = ["#fff5eb", "#fdd0a2", "#fd8d3c", "#e6550d", "#a63603", "#5e1a02"];
const INVALID_COLOR = "#c9d3d8";

function hexToRgb(hex) {
  const n = parseInt(hex.slice(1), 16);
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
}
const RAMP_RGB = RAMP.map(hexToRgb);

// Colour for t in 0..1, interpolated along RAMP.
export function rampColor(t) {
  const x = Math.min(1, Math.max(0, t)) * (RAMP_RGB.length - 1),
    i = Math.min(RAMP_RGB.length - 2, Math.floor(x)),
    f = x - i;
  const [a, b] = [RAMP_RGB[i], RAMP_RGB[i + 1]];
  return `rgb(${a.map((v, k) => Math.round(v + (b[k] - v) * f)).join(",")})`;
}

// Picture from the API -> {width, height, temps (null = no reading), min, max, hottest}.
export function decodeFrame(frame) {
  const bytes = Uint8Array.from(atob(frame.pixels), (c) => c.charCodeAt(0));
  if (bytes.length !== frame.width * frame.height) throw new Error("pixel count mismatch");
  const temps = Array.from(bytes, (v) =>
    v === frame.invalid ? null : frame.base_c + v * frame.step_c,
  );
  let min = Infinity,
    max = -Infinity,
    hottest = -1;
  temps.forEach((t, i) => {
    if (t === null) return;
    min = Math.min(min, t);
    if (t > max) [max, hottest] = [t, i];
  });
  return { width: frame.width, height: frame.height, temps, min, max, hottest };
}

// Draws the picture to fill `canvas`, keeping square pixels. Returns the cell
// geometry so the caller can map the mouse to a pixel.
export function drawHeatmap(canvas, picture) {
  const rect = canvas.getBoundingClientRect();
  if (rect.width < 50 || !picture) return null;
  const ratio = window.devicePixelRatio || 1;
  canvas.width = rect.width * ratio;
  canvas.height = rect.height * ratio;
  const ctx = canvas.getContext("2d");
  ctx.scale(ratio, ratio);
  const cell = Math.min(rect.width / picture.width, rect.height / picture.height),
    left = (rect.width - cell * picture.width) / 2,
    top = (rect.height - cell * picture.height) / 2,
    span = Math.max(picture.max - picture.min, 0.1);
  picture.temps.forEach((t, i) => {
    const x = left + (i % picture.width) * cell,
      y = top + Math.floor(i / picture.width) * cell;
    ctx.fillStyle = t === null ? INVALID_COLOR : rampColor((t - picture.min) / span);
    // 1px overlap avoids hairline seams between cells.
    ctx.fillRect(x, y, cell + 0.5, cell + 0.5);
  });
  if (picture.hottest >= 0) {
    const x = left + ((picture.hottest % picture.width) + 0.5) * cell,
      y = top + (Math.floor(picture.hottest / picture.width) + 0.5) * cell;
    ctx.strokeStyle = "#fff";
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.arc(x, y, Math.max(4, cell * 0.6), 0, Math.PI * 2);
    ctx.stroke();
  }
  canvas.setAttribute(
    "aria-label",
    `Heat map ${picture.width} by ${picture.height}. Coldest ${picture.min.toFixed(1)} °C, hottest ${picture.max.toFixed(1)} °C.`,
  );
  return { cell, left, top };
}

// CSS gradient for the legend bar.
export const legendGradient = () => `linear-gradient(to right, ${RAMP.join(", ")})`;
