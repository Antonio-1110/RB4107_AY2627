// Formatting of values for display.

export const number = (value) => typeof value === "number" && Number.isFinite(value);

export const human = (value) =>
  value == null
    ? "Unknown"
    : String(value)
        .replaceAll("_", " ")
        .replace(/^./, (c) => c.toUpperCase());

export const seconds = (value) => (number(value) ? `${Math.round(value)} s` : "—");

export const when = (value) => {
  if (!value) return "—";
  const date = new Date(value),
    today = new Date();
  const sameDay = date.toDateString() === today.toDateString();
  return date.toLocaleString(
    [],
    sameDay
      ? { hour: "2-digit", minute: "2-digit", second: "2-digit" }
      : { month: "short", day: "numeric", hour: "2-digit", minute: "2-digit" },
  );
};

export const locationLine = (location) =>
  [
    location.site_name,
    location.building,
    location.access_zone,
    location.level,
    location.area,
    location.unit,
  ]
    .filter(Boolean)
    .join(" · ");
