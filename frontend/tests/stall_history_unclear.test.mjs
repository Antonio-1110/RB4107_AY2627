// Stall history's incident log: a buzzer warning while the pan was losing heat is "unclear" (the stove may
// already have been off). The "Show" choice lists them, hides them, or lists only them.
// Run: node --test frontend/tests/

import assert from "node:assert/strict";
import { test } from "node:test";
import { installDom } from "./fake_dom.mjs";

installDom();
globalThis.window = { addEventListener() {} }; // the log redraws its chart on resize
const { matchesFilter } = await import("../js/stall_history.js");

const log = [
  { id: 1, peak: "warning", unclear: false },
  { id: 2, peak: "warning", unclear: true },
  { id: 3, peak: "shutdown", unclear: false },
];
const ids = (filter) => log.filter((incident) => matchesFilter(incident, filter)).map((incident) => incident.id);

test("the log's Show choice: all, shut-offs, counted, unclear", () => {
  assert.deepEqual(ids(""), [1, 2, 3]);
  assert.deepEqual(ids("shutdown"), [3]);
  assert.deepEqual(ids("counted"), [1, 3]);
  assert.deepEqual(ids("unclear"), [2]);
});
