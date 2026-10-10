// Calls to the Django API (see django/ingest/views.py). Everything is read-only
// except C4002 radar tuning, the shutdown reset and the presence filter; the dashboard never trips a
// shutdown or drives the alarm or relay directly.

import { API_BASE, REQUEST_TIMEOUT_MS } from "./config.js";

async function get(path) {
  const response = await fetch(`${API_BASE}${path}`, {
    cache: "no-store",
    signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS),
  });
  if (!response.ok) throw new Error(`HTTP ${response.status}`);
  return response.json();
}

const device = (deviceId) => `/api/devices/${encodeURIComponent(deviceId)}`;

// All controllers, grouped into stalls, with summary counts and active alerts.
export const fetchFleet = () => get("/api/devices/");

// Temperature points for one controller over the last `hours`.
export const fetchHistory = (deviceId, hours, limit = 360) =>
  get(`${device(deviceId)}/history/?hours=${hours}&limit=${limit}`);

// Safety events for one controller over the last `hours`.
export const fetchEvents = (deviceId, hours = 24, limit = 30) =>
  get(`${device(deviceId)}/events/?hours=${hours}&limit=${limit}`);

async function post(path, body) {
  const response = await fetch(`${API_BASE}${path}`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
    signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS),
  });
  const result = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(result.error || `HTTP ${response.status}`);
  return result;
}

// Operator reset of a latched shutdown (docs/remote_reset.md). The controller's
// answer is reset_required going false in the next telemetry.
export const sendReset = (deviceId) => post(`${device(deviceId)}/reset/`, {});

// Presence filter until the controller reboots: {absence_ms, return_ms, return_gap_ms}
// or {defaults: true}. The answer is values.presence_filter in the next telemetry.
export const sendPresenceFilter = (deviceId, body) => post(`${device(deviceId)}/presence_filter/`, body);

// Remote C4002 tuning. The node's answer
// arrives later in the device's values (sensors.<node>.c4002).
export const sendC4002Command = (deviceId, node, body) =>
  post(`${device(deviceId)}/nodes/${encodeURIComponent(node)}/c4002/`, body);

// Raw C4002 results of one presence node (last minute, or only those after `after`).
export const fetchC4002Live = (deviceId, node, after = 0) =>
  get(`${device(deviceId)}/nodes/${encodeURIComponent(node)}/c4002/live/?after=${after}`);

// Latest heat-map picture of each thermal node of one controller (display only).
export const fetchThermalFrame = (deviceId) => get(`${device(deviceId)}/thermal_frame/`);
