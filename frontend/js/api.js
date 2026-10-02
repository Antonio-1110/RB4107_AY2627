// Calls to the Django API (see django/ingest/views.py). Everything is read-only
// except C4002 radar tuning; the dashboard never operates the alarm, relay or reset.

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

// Remote C4002 tuning (the only command the dashboard sends). The node's answer
// arrives later in the device's values (sensors.<node>.c4002).
export async function sendC4002Command(deviceId, node, body) {
  const response = await fetch(`${device(deviceId)}/nodes/${encodeURIComponent(node)}/c4002/`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
    signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS),
  });
  const result = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(result.error || `HTTP ${response.status}`);
  return result;
}
