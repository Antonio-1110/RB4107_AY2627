// Read-only calls to the Django API (see backend/django/ingest/views.py).
// The dashboard never sends commands to the controller.

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
