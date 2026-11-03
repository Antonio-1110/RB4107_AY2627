// A small stand-in for the browser, enough to run the dashboard's modules under `node --test`
// without a browser or any npm package. Elements are made on first getElementById(); whether the id
// really exists in index.html is checked by contract.test.mjs. Requests go to a recorded fake fetch.

const byId = new Map(); // every element given an id, as getElementById finds them

class Text {
  constructor(text) {
    this.textContent = String(text);
  }
}

export class El {
  constructor(tag = "div", id = "") {
    this.tagName = tag.toUpperCase();
    this._id = "";
    this.id = id;
    this.value = "";
    this.disabled = false;
    this.open = false;
    this.title = "";
    this.className = "";
    this.dataset = {};
    this.children = [];
    this.parentElement = null;
    this.listeners = {};
    this._text = "";
  }
  get id() {
    return this._id;
  }
  set id(id) {
    this._id = id;
    if (id) byId.set(id, this);
  }
  get textContent() {
    return this._text + this.children.map((c) => c.textContent).join("");
  }
  set textContent(text) {
    this._text = String(text);
    this.children = [];
  }
  get innerText() {
    return this.textContent;
  }
  get firstChild() {
    return this.children[0] || new Text(this._text);
  }
  get classList() {
    const names = () => this.className.split(/\s+/).filter(Boolean);
    return {
      add: (...add) => (this.className = [...new Set([...names(), ...add])].join(" ")),
      remove: (...gone) => (this.className = names().filter((n) => !gone.includes(n)).join(" ")),
      contains: (name) => names().includes(name),
    };
  }
  append(...nodes) {
    for (const n of nodes) {
      const node = typeof n === "string" ? new Text(n) : n;
      if (node.isFragment) {
        this.append(...node.children);
        continue;
      }
      node.parentElement = this;
      this.children.push(node);
    }
  }
  replaceChildren(...nodes) {
    this.children = [];
    this._text = "";
    this.append(...nodes);
  }
  closest(selector) {
    for (let el = this.parentElement; el; el = el.parentElement) if (el.tagName === selector.toUpperCase()) return el;
    return null;
  }
  addEventListener(type, fn) {
    (this.listeners[type] ||= []).push(fn);
  }
  dispatch(type) {
    for (const fn of this.listeners[type] || []) fn({ type, target: this });
  }
  click() {
    if (!this.disabled) this.dispatch("click");
  }
  // Type into a field the way a person does: change the value, then the input event.
  enter(value) {
    this.value = String(value);
    this.dispatch("input");
  }
}

export function installDom() {
  globalThis.document = {
    getElementById(id) {
      if (!byId.has(id)) {
        // In the page every field sits in a <label> whose first text is its name.
        const el = new El("input", id),
          label = new El("label");
        label._text = id;
        label.append(el);
      }
      return byId.get(id);
    },
    createElement: (tag) => new El(tag),
    createTextNode: (text) => new Text(text),
    createDocumentFragment: () => Object.assign(new El("fragment"), { isFragment: true }),
    querySelectorAll: () => [],
    documentElement: new El("html"),
  };
  globalThis.confirm = () => true;
}

// Every POST the dashboard makes: {path, body}. Answers with the next request_id.
export const requests = [];
let nextRequest = 100;
export function installFetch() {
  globalThis.fetch = async (url, options = {}) => {
    if (options.method === "POST") requests.push({ path: url, body: JSON.parse(options.body) });
    const request_id = nextRequest++;
    return { ok: true, status: 202, json: async () => ({ request_id }) };
  };
}

// Let the awaited fetch() calls finish.
export const settle = () => new Promise((resolve) => setTimeout(resolve, 0));
