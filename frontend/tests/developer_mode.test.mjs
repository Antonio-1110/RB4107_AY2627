// Developer mode: the tuning tools only open after the password (RB4107, a demo gate, not security), and
// after that the button switches straight back and forth until the browser tab closes.
// Run: node --test frontend/tests/

import assert from "node:assert/strict";
import { test } from "node:test";
import { El, installDom } from "./fake_dom.mjs";

installDom();
// What mode.js needs beyond fake_dom: attributes, focus, storage, the URL and the redraw hook.
Object.assign(El.prototype, {
  setAttribute(name, value) {
    (this.attrs ||= {})[name] = String(value);
  },
  getAttribute(name) {
    return (this.attrs || {})[name] ?? null;
  },
  focus() {},
  select() {},
});
const storage = () => {
  const items = new Map();
  return {
    getItem: (k) => (items.has(k) ? items.get(k) : null),
    setItem: (k, v) => items.set(k, String(v)),
    removeItem: (k) => items.delete(k),
  };
};
globalThis.localStorage = storage();
globalThis.sessionStorage = storage();
globalThis.location = { search: "?mode=tuning" }; // a link straight to Tuning while locked
globalThis.requestAnimationFrame = () => {};
globalThis.CustomEvent = class {};
globalThis.window = { dispatchEvent() {}, matchMedia: undefined };
document.body = new El("body");

const { $ } = await import("../js/dom.js");
const { getMode, DEVELOPER_PASSWORD } = await import("../js/mode.js");

const submit = (password) => {
  $("dev-password").value = password;
  for (const fn of $("dev-form").listeners.submit) fn({ preventDefault() {} });
};

test("locked: a /?mode=tuning link stays on Monitor and asks for the password", () => {
  assert.equal(getMode(), "monitor");
  assert.equal($("dev-form").hidden, false);
  assert.equal($("dev-toggle").getAttribute("aria-pressed"), "false");
});

test("a wrong password keeps Monitor and says so", () => {
  submit("rb4107");
  assert.equal(getMode(), "monitor");
  assert.equal($("dev-error").hidden, false);
  assert.equal(sessionStorage.getItem("rb4107.developer"), null);
});

test("the right password opens the tuning tools and is remembered for this tab", () => {
  assert.equal(DEVELOPER_PASSWORD, "RB4107");
  submit("RB4107");
  assert.equal(getMode(), "tuning");
  assert.equal($("dev-form").hidden, true);
  assert.equal($("dev-toggle").getAttribute("aria-pressed"), "true");
  assert.equal(sessionStorage.getItem("rb4107.developer"), "1");
});

test("once unlocked, the button switches back and forth without asking again", () => {
  $("dev-toggle").click();
  assert.equal(getMode(), "monitor");
  assert.equal($("dev-form").hidden, true);
  $("dev-toggle").click();
  assert.equal(getMode(), "tuning");
});
