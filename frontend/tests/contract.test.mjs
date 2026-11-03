// The page, the scripts and the stylesheet agree with each other: every element a script looks up exists
// once, the tabs and the grid layout line up, every colour variable is defined, and form fields that sit
// side by side are the same height. Several people (and Claude sessions) edit these files separately, and
// none of these mistakes shows up until someone opens the page.
// Run: node --test frontend/tests/

import assert from "node:assert/strict";
import { readFileSync, readdirSync } from "node:fs";
import { test } from "node:test";

const root = new URL("../", import.meta.url);
const read = (path) => readFileSync(new URL(path, root), "utf8");
const html = read("index.html");
const css = read("css/dashboard.css").replace(/\/\*[\s\S]*?\*\//g, "");
const scripts = Object.fromEntries(
  readdirSync(new URL("js/", root))
    .filter((name) => name.endsWith(".js"))
    .map((name) => [name, read(`js/${name}`)]),
);

// Every opening tag with its attributes.
const tags = [...html.matchAll(/<([a-z][\w-]*)\b([^>]*)>/gi)].map(([, tag, attrs]) => ({
  tag: tag.toLowerCase(),
  attr: Object.fromEntries([...attrs.matchAll(/([\w-]+)="([^"]*)"/g)].map(([, k, v]) => [k, v])),
}));
const ids = tags.map((t) => t.attr.id).filter(Boolean);

test("no id is used twice in index.html", () => {
  const twice = ids.filter((id, i) => ids.indexOf(id) !== i);
  assert.deepEqual(twice, []);
});

test("every element a script looks up by a fixed id is in index.html", () => {
  const missing = [];
  for (const [name, source] of Object.entries(scripts)) {
    // $("id"), set("id", …), pill("id", …), getElementById("id"); not params.set("key", …)
    for (const m of source.matchAll(/(?<![.\w$])(?:\$|set|pill)\(\s*"([\w-]+)"|getElementById\(\s*"([\w-]+)"/g)) {
      const id = m[1] ?? m[2];
      if (!ids.includes(id)) missing.push(`${name}: ${id}`);
    }
  }
  assert.deepEqual(missing, []);
});

test("the Safety tab has a container for every rb_params.def group and an Apply for every card", async () => {
  const { default: rules } = await import("../js/safety_rules_data.js");
  for (const group of new Set(rules.params.map((p) => p.group.toLowerCase())))
    assert.ok(ids.includes(`pr-fields-${group}`), `pr-fields-${group}`);
  const cards = (scripts["safety_save.js"].match(/const CARDS = \[([^\]]*)\]/) || [])[1];
  assert.ok(cards, "safety_save.js lists its CARDS");
  for (const [, card] of cards.matchAll(/"(\w+)"/g)) assert.ok(ids.includes(`save-${card}`), `save-${card}`);
});

test("the tab buttons, mode.js and the data-tab sections name the same tabs", () => {
  const known = [...scripts["mode.js"].match(/const TABS = \[([^\]]*)\]/)[1].matchAll(/"(\w+)"/g)].map((m) => m[1]);
  const buttons = tags.map((t) => t.attr["data-tab-button"]).filter(Boolean);
  assert.deepEqual([...buttons].sort(), [...known].sort());
  const used = new Set(tags.map((t) => t.attr["data-tab"]).filter(Boolean));
  for (const tab of used) assert.ok(known.includes(tab), `data-tab="${tab}" is not a tab in mode.js`);
  for (const tab of known) assert.ok(used.has(tab), `tab ${tab} shows nothing`);
});

// grid-template-areas of the stall page, per mode and tab: [[row of 12 names], …]
function templates() {
  const out = {};
  for (const [, selector, body] of css.matchAll(/(#detail-view\[[^{]*\.dv-grid)\s*\{([^}]*)\}/g)) {
    const areas = body.match(/grid-template-areas:([^;]*);/);
    if (!areas) continue;
    const mode = selector.match(/data-mode="(\w+)"/)[1],
      tab = (selector.match(/data-tab="(\w+)"/) || [])[1] || "*";
    out[`${mode}/${tab}`] = [...areas[1].matchAll(/"([^"]*)"/g)].map(([, row]) => row.trim().split(/\s+/));
  }
  return out;
}

// The grid area each stall-page block is placed in: `.dv-x { grid-area: … }` or `#id { grid-area: … }`.
function areaOf(attr) {
  for (const [, sel, area] of css.matchAll(/([.#][\w-]+)\s*\{\s*grid-area:\s*([\w-]+);\s*\}/g)) {
    if (sel === `#${attr.id}` || (sel[0] === "." && (attr.class || "").split(/\s+/).includes(sel.slice(1)))) return area;
  }
  return null;
}

test("every stall-page grid layout is 12 columns wide with rectangular areas", () => {
  const all = templates();
  assert.ok(Object.keys(all).length >= 5, "found the stall page layouts");
  for (const [name, rows] of Object.entries(all)) {
    for (const row of rows) assert.equal(row.length, 12, `${name}: ${row.join(" ")}`);
    for (const area of new Set(rows.flat())) {
      const cells = rows.flatMap((row, r) => row.map((a, c) => (a === area ? [r, c] : null)).filter(Boolean));
      const rs = cells.map(([r]) => r),
        cs = cells.map(([, c]) => c);
      const box = (Math.max(...rs) - Math.min(...rs) + 1) * (Math.max(...cs) - Math.min(...cs) + 1);
      assert.equal(box, cells.length, `${name}: area "${area}" is not a rectangle`);
    }
  }
});

test("every block a tuning tab shows has a place in that tab's grid", () => {
  const all = templates();
  const blocks = tags.filter((t) => t.attr["data-tab"] && areaOf(t.attr));
  assert.ok(blocks.length >= 6, "found the stall page blocks");
  for (const { attr } of blocks) {
    const layout = all[`tuning/${attr["data-tab"]}`];
    assert.ok(layout, `no tuning layout for tab ${attr["data-tab"]}`);
    assert.ok(layout.flat().includes(areaOf(attr)), `#${attr.id || attr.class} (${areaOf(attr)}) has no place in tuning/${attr["data-tab"]}`);
  }
  // The countdown is on every tab.
  for (const [name, rows] of Object.entries(all)) assert.ok(rows.flat().includes("hero"), `${name} has no countdown`);
});

test("every block Monitor shows has a place in the Monitor grid", () => {
  const monitor = templates()["monitor/*"];
  assert.ok(monitor, "Monitor layout");
  // Hidden in Monitor: data-show="tuning" (one rule for all), or a Monitor rule with display: none.
  const hidden = [...css.matchAll(/#detail-view\[data-mode="monitor"\]\s+([.#][\w-]+)\s*\{[^}]*display:\s*none/g)].map(
    ([, sel]) => sel,
  );
  const isHidden = (attr) =>
    attr["data-show"] === "tuning" ||
    hidden.some((sel) => sel === `#${attr.id}` || (attr.class || "").split(/\s+/).includes(sel.slice(1)));
  for (const t of tags.filter((t) => areaOf(t.attr) && t.attr.class && /\bdv-|countdown-card/.test(t.attr.class + t.attr.id))) {
    if (!isHidden(t.attr))
      assert.ok(monitor.flat().includes(areaOf(t.attr)), `#${t.attr.id || t.attr.class} shows in Monitor but has no place in its grid`);
  }
});

test("every colour variable the stylesheet uses is defined", () => {
  const defined = new Set([...css.matchAll(/(--[\w-]+)\s*:/g)].map(([, name]) => name));
  const used = new Set([...css.matchAll(/var\((--[\w-]+)\s*\)/g)].map(([, name]) => name));
  const missing = [...used].filter((name) => !defined.has(name));
  assert.deepEqual(missing, []);
});

test("the dark theme sets every colour the light theme sets", () => {
  const block = (selector) => {
    const at = css.indexOf(selector);
    assert.notEqual(at, -1, selector);
    const body = css.slice(css.indexOf("{", at) + 1, css.indexOf("}", at));
    return new Set([...body.matchAll(/(--[\w-]+)\s*:/g)].map(([, name]) => name));
  };
  const light = block(":root {"),
    dark = block('[data-theme="dark"]');
  const missing = [...light].filter((name) => !dark.has(name) && !/^--(radius|shadow|font)/.test(name));
  assert.deepEqual(missing, [], "colours with no dark-theme value stay light in dark mode");
});

test("form fields that sit side by side are the same height", () => {
  // height per "container element" from every rule that sets one, e.g. ".tuning-grid select" -> 36px
  const height = {};
  for (const [, selectors, body] of css.matchAll(/([^{}]+)\{([^}]*)\}/g)) {
    const h = body.match(/(?:^|;|\s)height:\s*([\d.]+px)/);
    if (!h) continue;
    for (const sel of selectors.split(",").map((s) => s.trim())) {
      const m = sel.match(/^(.*\S)\s+(select|input)$/);
      if (m) (height[m[1]] ||= {})[m[2]] = h[1];
    }
  }
  // Each container that holds both inputs and dropdowns in index.html must give them the same height.
  const containers = ["filter-card", "tuning-grid", "tuning-actions", "heatmap-footprint"];
  for (const name of containers) {
    const h = height[`.${name}`];
    assert.ok(h && h.input && h.select, `.${name} gives both input and select a height`);
    assert.equal(h.select, h.input, `.${name}: select ${h.select}, input ${h.input}`);
  }
  for (const [ctx, h] of Object.entries(height))
    if (h.input && h.select) assert.equal(h.select, h.input, `${ctx}: select ${h.select}, input ${h.input}`);
});

test("every dropdown in index.html is inside a container whose height rule covers it", () => {
  // Dropdowns outside the known containers would get the browser's own (shorter) height.
  const covered = /class="[^"]*\b(filter-card|tuning-grid|tuning-actions|heatmap-footprint|station-label|range-label|hist-filter|hist-filters)\b/;
  const opened = [];
  const missing = [];
  for (const m of html.matchAll(/<(\/?)([a-z][\w-]*)\b([^>]*?)(\/?)>/gi)) {
    const [, close, tag, attrs, self] = m;
    if (["input", "br", "img", "meta", "link", "option"].includes(tag.toLowerCase()) || self) continue;
    if (close) opened.pop();
    else {
      if (tag.toLowerCase() === "select" && !opened.some((a) => covered.test(a)))
        missing.push((attrs.match(/id="([^"]*)"/) || [])[1] || "select");
      opened.push(attrs);
    }
  }
  assert.deepEqual(missing, []);
});

test("every font size comes from the type scale", () => {
  // One size per kind of text: --fs-* at the top of dashboard.css. A raw px size is allowed only for an
  // icon glyph, marked "icon glyph" in a comment on the same line.
  const raw = read("css/dashboard.css");
  const scale = new Set([...raw.matchAll(/(--fs-[\w-]+)\s*:/g)].map(([, name]) => name));
  const bad = [];
  const lines = raw.split("\n");
  lines.forEach((line, i) => {
    const m = line.match(/^\s*font(?:-size)?\s*:\s*([^;]*)/);
    if (!m || /^inherit\b/.test(m[1]) || /icon glyph/.test(line)) return;
    const value = m[1].trim() || lines[i + 1].trim(); // "font:" with its value on the next line
    const v = value.match(/^var\((--fs-[\w-]+)\)/);
    if (!v || !scale.has(v[1])) bad.push(`line ${i + 1}: ${line.trim()}`);
  });
  assert.deepEqual(bad, [], "use a --fs-* variable (see the type scale at the top of dashboard.css)");
});

test("text drawn on a canvas takes its size from the type scale", () => {
  const bad = Object.entries(scripts).flatMap(([name, src]) =>
    [...src.matchAll(/\.font\s*=\s*["'`]\d/g)].map(() => name),
  );
  assert.deepEqual(bad, [], 'use `${cssVar("--fs-meta")} system-ui`, not a px size');
});
