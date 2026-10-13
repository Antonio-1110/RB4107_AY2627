"use strict";
// Monitor / Tuning mode, tuning tabs, overview layout and colour theme.
// All of it is cosmetic: Monitor hides the tuning tools, it does not lock the API
// (docs/dashboard.md). The choices are remembered in this browser only.

import { $ } from "./dom.js";

const KEYS = { mode: "rb4107.mode", tab: "rb4107.tab", theme: "rb4107.theme", overview: "rb4107.overview" };
const TABS = ["rules", "radar", "thermal", "diag"];
const THEMES = ["auto", "light", "dark"];
const THEME_LABEL = { auto: "Auto", light: "Light", dark: "Dark" };

function read(key) {
  try {
    return localStorage.getItem(key);
  } catch {
    return null; // private window or blocked storage: the page works without it
  }
}
function write(key, value) {
  try {
    localStorage.setItem(key, value);
  } catch {
    /* not remembered */
  }
}

const pick = (value, allowed, fallback) => (allowed.includes(value) ? value : fallback);
let mode = pick(new URLSearchParams(location.search).get("mode") || read(KEYS.mode), ["monitor", "tuning"], "monitor"),
  tab = pick(read(KEYS.tab), TABS, "rules"),
  theme = pick(read(KEYS.theme), THEMES, "auto"),
  overview = pick(read(KEYS.overview), ["tiles", "table"], "tiles");
const listeners = [];

export const getMode = () => mode;
export const isTuning = () => mode === "tuning";
export const onModeChange = (callback) => listeners.push(callback);

// Canvases change size with the mode, tab and theme: ask them to redraw once the layout has settled.
const redraw = () => requestAnimationFrame(() => window.dispatchEvent(new CustomEvent("rb-redraw")));

function apply() {
  const detail = $("detail-view");
  detail.dataset.mode = mode;
  detail.dataset.tab = tab;
  document.body.dataset.mode = mode;
  $("overview-view").dataset.ov = overview;
  for (const button of document.querySelectorAll("[data-mode-button]"))
    button.setAttribute("aria-pressed", String(button.dataset.modeButton === mode));
  for (const button of document.querySelectorAll("[data-tab-button]"))
    button.setAttribute("aria-pressed", String(button.dataset.tabButton === tab));
  for (const button of document.querySelectorAll("[data-ov-button]"))
    button.setAttribute("aria-pressed", String(button.dataset.ovButton === overview));
  if (theme === "auto") delete document.documentElement.dataset.theme;
  else document.documentElement.dataset.theme = theme;
  const toggle = $("theme-toggle");
  toggle.textContent = THEME_LABEL[theme];
  toggle.setAttribute("aria-label", `Colour theme: ${THEME_LABEL[theme].toLowerCase()}. Click to change.`);
}

function change(key, value, apply_) {
  write(KEYS[key], value);
  apply_(value);
  apply();
  redraw();
  for (const callback of listeners) callback(mode);
}

for (const button of document.querySelectorAll("[data-mode-button]"))
  button.addEventListener("click", () => change("mode", button.dataset.modeButton, (v) => (mode = v)));
for (const button of document.querySelectorAll("[data-tab-button]"))
  button.addEventListener("click", () => change("tab", button.dataset.tabButton, (v) => (tab = v)));
for (const button of document.querySelectorAll("[data-ov-button]"))
  button.addEventListener("click", () => change("overview", button.dataset.ovButton, (v) => (overview = v)));
$("theme-toggle").addEventListener("click", () =>
  change("theme", THEMES[(THEMES.indexOf(theme) + 1) % THEMES.length], (v) => (theme = v)),
);
window.matchMedia?.("(prefers-color-scheme: dark)").addEventListener?.("change", redraw);

apply();
