"use strict";
// Monitor / Tuning mode, tuning tabs, overview layout and colour theme.
// All of it is cosmetic: Monitor hides the tuning tools, it does not lock the API
// (docs/dashboard.md). The choices are remembered in this browser only.
//
// Tuning is developer mode, which asks for a password the first time. The password is not a secret and
// this is not security (anyone can still call the API): it keeps the commissioning tools out of a stall
// operator's or inspector's way during the demo. Developer mode lasts until this browser tab is closed.

import { $ } from "./dom.js";

const KEYS = { mode: "rb4107.mode", tab: "rb4107.tab", theme: "rb4107.theme", overview: "rb4107.overview" };
const DEVELOPER_KEY = "rb4107.developer"; // in sessionStorage: forgotten when the tab closes
export const DEVELOPER_PASSWORD = "RB4107";
const TABS = ["rules", "radar", "thermal", "diag", "logs"];
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

function readSession(key) {
  try {
    return sessionStorage.getItem(key);
  } catch {
    return null;
  }
}
function writeSession(key, value) {
  try {
    if (value === null) sessionStorage.removeItem(key);
    else sessionStorage.setItem(key, value);
  } catch {
    /* lasts until the page reloads */
  }
}

const pick = (value, allowed, fallback) => (allowed.includes(value) ? value : fallback);
let developer = readSession(DEVELOPER_KEY) === "1";
const linked = new URLSearchParams(location.search).get("mode"),
  asked = pick(linked || read(KEYS.mode), ["monitor", "tuning"], "monitor");
let mode = developer ? asked : "monitor",
  tab = pick(read(KEYS.tab), TABS, "rules"),
  theme = pick(read(KEYS.theme), THEMES, "auto"),
  overview = pick(read(KEYS.overview), ["tiles", "table"], "tiles");
const listeners = [];

export const getMode = () => mode;
export const isTuning = () => mode === "tuning";
export const getTab = () => tab;
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
  $("dev-toggle").setAttribute("aria-pressed", String(mode === "tuning"));
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

// The Developer mode button turns the tuning tools on and off. The first time in this tab it asks for the
// password; after that it switches straight back and forth.
function showForm(open) {
  $("dev-form").hidden = !open;
  $("dev-toggle").setAttribute("aria-expanded", String(open));
  $("dev-error").hidden = true;
  $("dev-password").value = "";
  if (open) $("dev-password").focus();
}
$("dev-toggle").addEventListener("click", () => {
  if (!developer) showForm($("dev-form").hidden);
  else change("mode", mode === "tuning" ? "monitor" : "tuning", (v) => (mode = v));
});
$("dev-form").addEventListener("submit", (event) => {
  event.preventDefault();
  if ($("dev-password").value !== DEVELOPER_PASSWORD) {
    $("dev-error").hidden = false;
    $("dev-password").select();
    return;
  }
  developer = true;
  writeSession(DEVELOPER_KEY, "1");
  showForm(false);
  change("mode", "tuning", (v) => (mode = v));
});
$("dev-cancel").addEventListener("click", () => showForm(false));
$("dev-form").addEventListener("keydown", (event) => {
  if (event.key === "Escape") showForm(false);
});

apply();
// A link to /?mode=tuning while locked: stay on Monitor and ask for the password.
if (!developer && linked === "tuning") showForm(true);
