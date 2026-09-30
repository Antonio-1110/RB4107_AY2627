// Small DOM helpers shared by every part of the dashboard.

export const $ = (id) => document.getElementById(id);

export function set(id, value) {
  $(id).textContent = value;
}

export function pill(id, text, style) {
  $(id).className = `pill ${style}`;
  set(id, text);
}

export function make(tag, text, className) {
  const node = document.createElement(tag);
  if (text != null) node.textContent = text;
  if (className) node.className = className;
  return node;
}

export function makeBadge(text, style) {
  return make("span", text, `state-badge ${style}`);
}
