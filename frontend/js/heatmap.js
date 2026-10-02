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
  return {
    width: frame.width,
    height: frame.height,
    temps,
    min,
    max,
    hottest,
    threshold: typeof frame.hot_threshold_c === "number" ? frame.hot_threshold_c : null,
    radius: Number.isInteger(frame.hot_region_radius) ? frame.hot_region_radius : null,
  };
}

// Pixels at or above the node's hot-pixel threshold, counted from the picture.
// It can differ by a pixel or two from the node's own count, because the
// picture's pixels are rounded to the picture's step.
export const pixelsAboveThreshold = (picture) =>
  picture.threshold === null
    ? null
    : picture.temps.filter((t) => t !== null && t >= picture.threshold).length;

// Size of one pixel (cm) at distance_m for a sensor with the given field of
// view (degrees, horizontal x vertical). Pixels in the middle of the picture;
// the wide lens's edge pixels are larger.
export function pixelFootprintCm(fov, distanceM, cols = 32, rows = 24) {
  const side = (deg, n) => ((2 * distanceM * Math.tan((deg * Math.PI) / 360)) / n) * 100;
  return { width: side(fov[0], cols), height: side(fov[1], rows) };
}

// Draws the picture to fill `canvas`, keeping square pixels. Returns the cell
// geometry so the caller can map the mouse to a pixel.
// Overlays: `showThreshold` outlines pixels at or above the node's threshold,
// `showRegion` draws the node's hot region (hottest pixel +/- radius).
export function drawHeatmap(canvas, picture, { showThreshold = true, showRegion = true } = {}) {
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
  if (showThreshold && picture.threshold !== null) {
    ctx.strokeStyle = "#0b4f6c";
    ctx.lineWidth = 1.5;
    picture.temps.forEach((t, i) => {
      if (t === null || t < picture.threshold) return;
      const x = left + (i % picture.width) * cell,
        y = top + Math.floor(i / picture.width) * cell;
      ctx.strokeRect(x + 1, y + 1, cell - 2, cell - 2);
    });
  }
  if (showRegion && picture.hottest >= 0 && picture.radius !== null) {
    const col = picture.hottest % picture.width,
      row = Math.floor(picture.hottest / picture.width),
      c0 = Math.max(0, col - picture.radius),
      r0 = Math.max(0, row - picture.radius),
      c1 = Math.min(picture.width - 1, col + picture.radius),
      r1 = Math.min(picture.height - 1, row + picture.radius);
    const box = [left + c0 * cell, top + r0 * cell, (c1 - c0 + 1) * cell, (r1 - r0 + 1) * cell];
    ctx.save();
    // White underlay keeps the dashes visible over dark pixels and outlines.
    ctx.strokeStyle = "#fff";
    ctx.lineWidth = 5;
    ctx.strokeRect(...box);
    ctx.setLineDash([6, 4]);
    ctx.strokeStyle = "#142936";
    ctx.lineWidth = 2.5;
    ctx.strokeRect(...box);
    ctx.restore();
  }
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
