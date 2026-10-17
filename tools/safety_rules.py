#!/usr/bin/env python3
"""Generate docs/safety_rules.md and the dashboard's copy from the safety rules table, and check them.

The state machine runs from firmware/components/safety/src/safety_rules.def,
so the doc is generated from the same table the firmware executes.

It also reads the dashboard settings table, firmware/components/rb_params/include/rb_params.def
(every setting in the rules that has no dashboard card of its own), for Django's and the dashboard's
copy and the command schema.

  python3 tools/safety_rules.py gen     write docs/safety_rules.md, frontend/js/safety_rules_data.js,
                                        django/ingest/safety_params.json and the "params" keys of
                                        docs/schema/rb4107_mqtt.schema.json
  python3 tools/safety_rules.py check   exit 1 if any of those is out of date, the table names
                                        something that doesn't exist, a setting in the rules
                                        can't be changed from the dashboard, or a function the
                                        table names changed on this branch without the table
                                        changing

The branch comparison uses SAFETY_RULES_BASE (default origin/main) and is
skipped when git or that ref is not available.
"""
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
COMPONENTS = ROOT / "firmware" / "components"
DEF = COMPONENTS / "safety" / "src" / "safety_rules.def"
SAFETY_C = COMPONENTS / "safety" / "src" / "safety.c"
KCONFIG = COMPONENTS / "rb_config" / "Kconfig"
DOC = ROOT / "docs" / "safety_rules.md"
JS = ROOT / "frontend" / "js" / "safety_rules_data.js"
PARAMS_DEF = COMPONENTS / "rb_params" / "include" / "rb_params.def"
PARAMS_JSON = ROOT / "django" / "ingest" / "safety_params.json"
SCHEMA = ROOT / "docs" / "schema" / "rb4107_mqtt.schema.json"
LOGIC_JS = ROOT / "frontend" / "js" / "safety_logic.js"
# Wire units of rb_params.def: (scale from SI, unit shown).
KINDS = {"MS": (1000, " s"), "DC": (10, " °C"), "DC_PER_MIN": (10, " °C/min"), "TENTHS": (10, ""), "COUNT": (1, ""),
         "BOOL": (1, ""), "CHOICE": (1, "")}

SECTIONS = {
    "A": "A. What the controller believes about its inputs",
    "B": "B. Cooking on or off (hottest pixel)",
    "C": "C. Timers",
    "D": "D. Holds and returns",
    "X": "X. Outside the state machine",
}
STATE_NAMES = ["BOOT", "SELF_TEST", "IDLE", "MONITORING", "UNATTENDED", "WARNING", "SHUTDOWN", "FAULT"]
BUZZER = {"SAFETY_BUZZER_OFF": "off", "SAFETY_BUZZER_WARNING": "warning pattern",
          "SAFETY_BUZZER_SHUTDOWN": "shutdown pattern", "SAFETY_BUZZER_FAULT": "fault pattern"}


class Error(Exception):
    pass


# ---- parsing ----

def strip_c_comments(text):
    out, i, n = [], 0, len(text)
    while i < n:
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text[i] == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def split_args(body):
    args, depth, cur, i = [], 0, [], 0
    while i < len(body):
        ch = body[i]
        if ch == '"':
            j = i + 1
            while body[j] != '"':
                j += 2 if body[j] == "\\" else 1
            cur.append(body[i:j + 1])
            i = j + 1
            continue
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        elif ch == "," and depth == 0:
            args.append("".join(cur).strip())
            cur = []
            i += 1
            continue
        cur.append(ch)
        i += 1
    args.append("".join(cur).strip())
    return args


def c_string(arg):
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', arg)
    if not parts:
        raise Error(f"expected a string, got {arg!r}")
    return "".join(parts).replace('\\"', '"').replace("\\\\", "\\")


def macro_entries(text, names):
    entries = []
    for m in re.finditer(r"\b(" + "|".join(names) + r")\s*\(", text):
        start, depth, i = m.end(), 1, m.end()
        while depth:
            if text[i] == '"':
                i += 1
                while text[i] != '"':
                    i += 2 if text[i] == "\\" else 1
            elif text[i] == "(":
                depth += 1
            elif text[i] == ")":
                depth -= 1
            i += 1
        entries.append((m.group(1), split_args(text[start:i - 1])))
    return entries


def parse_def():
    entries = macro_entries(strip_c_comments(DEF.read_text()), ("SAFETY_STATE", "SAFETY_RULE", "SAFETY_SIGNAL"))
    states, rules, signals = [], [], []
    for kind, a in entries:
        if kind == "SAFETY_STATE":
            states.append({"state": a[0].removeprefix("SAFETY_"), "buzzer": a[1], "shutdown": a[2] == "true"})
        elif kind == "SAFETY_RULE":
            rules.append({"id": a[0], "from": re.findall(r"ST\(SAFETY_(\w+)\)", a[1]), "guard": a[2],
                          "to": a[3].removeprefix("SAFETY_"), "action": None if a[4] == "NULL" else a[4],
                          "reason": c_string(a[5]), "when": c_string(a[6])})
        else:
            signals.append({"id": a[0], "file": c_string(a[1]), "function": a[2],
                            "when": c_string(a[3]), "result": c_string(a[4])})
    return states, rules, signals


def parse_kconfig():
    text = KCONFIG.read_text()
    syms, choice = {}, None
    for m in re.finditer(r"^\s*(config|choice|endchoice)\b[ \t]*(\w*)\n((?:(?!^\s*(?:config|choice|endchoice|menu|endmenu|if|endif)\b).*\n)*)",
                         text, re.M):
        kw, sym, body = m.groups()
        if kw == "endchoice":
            choice = None
            continue
        prompt = re.search(r'^\s*(?:bool|int|string|hex|prompt)\s+"([^"]*)"', body, re.M)
        default = re.search(r"^\s*default\s+(\S+)", body, re.M)
        syms[sym] = {"kind": kw, "prompt": prompt.group(1) if prompt else "",
                     "default": default.group(1) if default else None, "choice": choice}
        if kw == "choice":
            choice = sym
    return syms


def parse_constants():
    out = {}
    for m in re.finditer(r"^#define[ \t]+([A-Z][A-Z0-9_]+)[ \t]+([0-9.]+)[uUfF]*[ \t]*(?:/\*[ \t]*(.*?)[ \t]*\*/)?[ \t]*$",
                         SAFETY_C.read_text(), re.M):
        out[m.group(1)] = (m.group(2), m.group(3) or "")
    return out


def si_value(name, kconfig, consts):
    """A Kconfig default or safety.c constant in seconds, degC, degC/min or plain numbers."""
    if name in consts:
        raw = consts[name][0]
    elif name in kconfig and kconfig[name]["default"] is not None and kconfig[name]["kind"] == "config":
        raw = kconfig[name]["default"]
    else:
        raise Error(f"rb_params.def: {name} is neither a Kconfig option with a default nor a safety.c constant")
    for suffix, scale in (("_DC_PER_MIN", 10), ("_DC", 10), ("_MS", 1000)):
        if name.endswith(suffix):
            return float(raw) / scale
    return float(raw)


def parse_params(kconfig, consts):
    """rb_params.def rows, with their menuconfig/built-in default in wire units."""
    raw = PARAMS_DEF.read_text()
    groups = [g for line in raw.splitlines()
              for g in ([re.match(r"^/\* ([A-Z][^*]*?) \*/\s*$", line).group(1)] if re.match(r"^/\* ([A-Z][^*]*?) \*/\s*$", line)
                        else ["@"] if line.startswith("RB_PARAM(") else [])]
    group_of, current = [], None
    for g in groups:
        if g == "@":
            group_of.append(current)
        else:
            current = g
    params = []
    for n, (_, a) in enumerate(macro_entries(strip_c_comments(raw), ("RB_PARAM",))):
        key, kind, lo, hi, source, options, label = a[0], a[1], int(a[2]), int(a[3]), a[4], c_string(a[5]), c_string(a[6])
        if kind not in KINDS:
            raise Error(f"rb_params.def {key}: unknown kind {kind}")
        labels = None
        if kind == "CHOICE":
            members = options.split("|")
            missing = [m for m in members if m not in kconfig]
            if missing or kconfig.get(source, {}).get("kind") != "choice":
                raise Error(f"rb_params.def {key}: {source} must be a Kconfig choice with members {members}")
            labels = [kconfig[m]["prompt"] for m in members]
            default = members.index(kconfig[source]["default"])
        elif kind == "BOOL":
            if source not in kconfig or kconfig[source]["default"] not in ("y", "n"):
                raise Error(f"rb_params.def {key}: {source} must be a Kconfig bool")
            labels, default = options.split("|"), 1 if kconfig[source]["default"] == "y" else 0
        else:
            expr = re.sub(r"[A-Za-z_]\w*", lambda m: repr(si_value(m.group(0), kconfig, consts)), source)
            if not re.fullmatch(r"[\d.\s+\-e]+", expr):
                raise Error(f"rb_params.def {key}: source may only add and subtract names")
            default = round(eval(expr) * KINDS[kind][0])  # numbers, + and - only (checked above)
        if not lo <= default <= hi:
            raise Error(f"rb_params.def {key}: the default {default} is outside {lo}..{hi}")
        params.append({"key": key, "kind": kind, "min": lo, "max": hi, "default": default, "label": label,
                       "source": source, "options": labels, "group": group_of[n] if n < len(group_of) else None})
    return params


def param_text(p, v):
    if p["options"]:
        return p["options"][v]
    scale, unit = KINDS[p["kind"]]
    return fmt_num(v / scale) + unit


def schema_with_params(text, params):
    """The schema with the controller_command "params" keys rewritten from rb_params.def (one line each)."""
    start = text.index('    "controller_command": {')
    props = text.index('      "properties": {\n', start) + len('      "properties": {\n')
    end = text.index("\n      },\n", props)
    keys = {p["key"] for p in params}
    lines = [ln for ln in text[props:end].split("\n")
             if not (re.match(r'^\s*"(\w+)":', ln) and re.match(r'^\s*"(\w+)":', ln).group(1) in keys)]
    lines[-1] = lines[-1].rstrip(",") + ","
    for i, p in enumerate(params):
        desc = f"params: {p['label']}" + (f" (0 = {p['options'][0]}, 1 = {p['options'][1]})" if p["options"] else
                                          f" ({p['kind'].lower().replace('_', ' ')})")
        lines.append(f'        "{p["key"]}": {json.dumps({"type": "integer", "minimum": p["min"], "maximum": p["max"], "description": desc}, ensure_ascii=False)}'
                     + ("," if i < len(params) - 1 else ""))
    return text[:props] + "\n".join(lines) + text[end:]


def dashboard_card_settings():
    """The settings the dashboard's own cards change: LIVE entries marked tune: true in safety_logic.js."""
    return set(re.findall(r"\b([A-Z][A-Z0-9_]+): \{\s*tune: true", LOGIC_JS.read_text()))


# ---- rendering ----

def fmt_num(v):
    v = float(v)
    return str(int(v)) if v == int(v) else f"{v:g}"


def with_unit(name, raw):
    v = float(raw)
    for suffix, scale, unit in (("_DC_PER_MIN", 10, " °C/min"), ("_DC", 10, " °C"), ("_C_PER_MIN", 1, " °C/min"),
                                ("_C", 1, " °C"), ("_MS", 1000, " s"), ("_S", 1, " s"), ("_PCT", 1, " %"),
                                ("_CM", 1, " cm"), ("_TAUS", 1, "")):
        if name.endswith(suffix):
            return fmt_num(v / scale) + unit
    return fmt_num(v)


def resolve(name, kconfig, consts):
    if name in consts:
        return with_unit(name, consts[name][0])
    if name.startswith("CONFIG_"):
        name = name[7:]
    sym = kconfig.get(name)
    if sym is None:
        raise Error(f"{{{name}}} is neither a Kconfig option nor a safety.c constant")
    if sym["kind"] == "choice":
        return f"\"{kconfig[sym['default']]['prompt']}\""
    if sym["default"] in ("y", "n"):
        return "on" if sym["default"] == "y" else "off"
    return with_unit(name, sym["default"])


def render_text(text, kconfig, consts, used, live=False):
    """{NAME} -> **default**, or for the dashboard {{NAME|default}} so it can show the live value instead."""
    def sub(m):
        used.add(m.group(1))
        value = resolve(m.group(1), kconfig, consts)
        return f"{{{{{m.group(1)}|{value}}}}}" if live else f"**{value}**"
    return re.sub(r"\{(\w+)\}", sub, text)


def generate():
    states, rules, signals = parse_def()
    kconfig, consts = parse_kconfig(), parse_constants()
    params = parse_params(kconfig, consts)
    used = set()
    out = [
        "# Safety rules: what the controller does, and when",
        "",
        "<!-- Generated by tools/safety_rules.py from firmware/components/safety/src/safety_rules.def. Do not edit: change the table and run `python3 tools/safety_rules.py gen`. -->",
        "",
        "Every condition the S3 controller reacts to, and what it does. The state",
        "machine runs from the same table this page is generated from",
        "([`safety_rules.def`](../firmware/components/safety/src/safety_rules.def)), so",
        "the transitions below are exactly what the firmware does. Values in **bold**",
        "are the menuconfig defaults; the dashboard can change every one of them (see the",
        "last section) and shows the values in force. The",
        "reasons behind the rules are in [safety_state_machine.md](safety_state_machine.md).",
        "",
        "## Outputs per state",
        "",
        "| State | Buzzer | Shutdown output (latched) |",
        "|---|---|---|",
    ]
    for s in states:
        out.append(f"| {s['state']} | {BUZZER[s['buzzer']]} | {'**active**' if s['shutdown'] else 'released'} |")

    def signal_section(letter):
        rows = [s for s in signals if s["id"][0] == letter]
        if not rows:
            return
        out.extend(["", f"## {SECTIONS[letter]}", "", "| # | When | Result | Code |", "|---|---|---|---|"])
        for s in rows:
            out.append(f"| {s['id']} | {render_text(s['when'], kconfig, consts, used)} | "
                       f"{render_text(s['result'], kconfig, consts, used)} | `{s['function']}()` in `{s['file']}` |")

    for letter in "ABCD":
        signal_section(letter)
    data = {
        "states": [{"state": s["state"], "buzzer": BUZZER[s["buzzer"]], "shutdown": s["shutdown"]} for s in states],
        "sections": [{"key": letter, "title": SECTIONS[letter],
                      "rows": [{"id": s["id"], "when": render_text(s["when"], kconfig, consts, set(), True),
                                "result": render_text(s["result"], kconfig, consts, set(), True), "code": f"{s['function']}()"}
                               for s in signals if s["id"][0] == letter]} for letter in SECTIONS],
        "transitions": [{"id": r["id"], "from": r["from"], "to": r["to"], "reason": r["reason"],
                         "when": render_text(r["when"], kconfig, consts, set(), True)} for r in rules],
        "params": params,
    }

    out.extend(["", "## T. State transitions", "",
                "Checked on every step in this order; the first rule of the current state that holds is taken.",
                "", "| # | From | When | To | Reported reason |", "|---|---|---|---|---|"])
    for r in rules:
        frm = ", ".join(r["from"]) if len(r["from"]) < 4 else "any cooking-related state (" + ", ".join(r["from"]) + ")"
        out.append(f"| {r['id']} | {frm} | {render_text(r['when'], kconfig, consts, used)} | **{r['to']}** | {r['reason']} |")
    out.append("")
    out.append("SHUTDOWN is left only by T16. A state not in the table goes to FAULT (\"invalid state\").")

    signal_section("X")

    settings = sorted({u for u in used if u not in consts} |
                      {k for k, v in kconfig.items() if k.startswith("RB_SAFETY_") and v["choice"] is None})
    out.extend(["", "## Settings", "", "Menuconfig: RB4107 configuration. Values in Kconfig units.", "",
                "| Setting | Default | What it is |", "|---|---|---|"])
    for k in settings:
        sym = kconfig[k]
        default = sym["default"] if sym["kind"] != "choice" else kconfig[sym["default"]]["prompt"]
        out.append(f"| `{k}` | {default} | {sym['prompt']} |")
    out.extend(["", "## Fixed constants in safety.c", "", "| Constant | Value | Note |", "|---|---|---|"])
    for k, (v, note) in consts.items():
        out.append(f"| `{k}` | {v} | {note} |")
    out.extend(["", "## Changing them from the dashboard", "",
                "The dashboard's Tuning cards change the cooking temperature, cookware profile, temperature tiers,",
                "presence filter and buzzer/shutdown times, fusion and pause budget, and the radar rules. Every other",
                "setting above is on the Logic tab's **Other settings** card",
                "([`rb_params.def`](../firmware/components/rb_params/include/rb_params.def)); the controller saves",
                "them until **Defaults** is pressed.", "",
                "| Setting | Key | Default | Range |", "|---|---|---|---|"])
    for p in params:
        rng = " / ".join(p["options"]) if p["options"] else f"{param_text(p, p['min'])} to {param_text(p, p['max'])}"
        out.append(f"| {p['label']} | `{p['key']}` | {param_text(p, p['default'])} | {rng} |")
    out.append("")
    js = ("// Generated by tools/safety_rules.py from firmware/components/safety/src/safety_rules.def.\n"
          "// Do not edit: change the table and run `python3 tools/safety_rules.py gen`.\n"
          "// {{NAME|default}} is a Kconfig option or safety.c constant with its default; the dashboard\n"
          "// shows the value the controller reports instead when it reports one.\n"
          f"export default {json.dumps(data, indent=1, ensure_ascii=False)};\n")
    django = json.dumps({"_": "Generated by tools/safety_rules.py from firmware/components/rb_params/include/rb_params.def; "
                              "do not edit.", "params": params}, indent=1, ensure_ascii=False) + "\n"
    schema = schema_with_params(SCHEMA.read_text(), params)
    uncovered = sorted(used - {p["source"] for p in params} - dashboard_card_settings())
    return "\n".join(out), states, rules, signals, js, django, schema, uncovered


# ---- checks ----

def function_spans(path):
    """{function name: (first line, last line)} for top-level definitions, 1-based."""
    lines = path.read_text().splitlines()
    spans, i = {}, 0
    while i < len(lines):
        m = re.match(r"^[A-Za-z_][\w \*]*?\b(\w+)\s*\(", lines[i])
        if m and not lines[i].rstrip().endswith(";") and not lines[i].startswith(("#", " ", "\t")):
            j = i
            while j < len(lines) and "{" not in lines[j] and not lines[j].rstrip().endswith(";"):
                j += 1
            if j < len(lines) and "{" in lines[j]:
                depth = 0
                for k in range(j, len(lines)):
                    depth += lines[k].count("{") - lines[k].count("}")
                    if depth == 0:
                        spans[m.group(1)] = (i + 1, k + 1)
                        i = k
                        break
        i += 1
    return spans


def changed_lines(base, path):
    out = subprocess.run(["git", "diff", "-U0", base, "--", str(path)], cwd=ROOT, capture_output=True, text=True)
    lines = set()
    for m in re.finditer(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@", out.stdout, re.M):
        start, count = int(m.group(1)), int(m.group(2) or 1)
        lines.update(range(start, start + max(count, 1)))
    return lines


def git_ok(base):
    r = subprocess.run(["git", "rev-parse", "--verify", "--quiet", base], cwd=ROOT, capture_output=True)
    return r.returncode == 0


def check():
    text, states, rules, signals, js, django, schema, uncovered = generate()
    errors = []
    if len(states) != len(STATE_NAMES) or [s["state"] for s in states] != STATE_NAMES:
        errors.append("SAFETY_STATE entries must list every state once, in enum order")
    ids = [r["id"] for r in rules] + [s["id"] for s in signals]
    for dup in sorted({i for i in ids if ids.count(i) > 1}):
        errors.append(f"rule id {dup} used twice")

    safety_spans = function_spans(SAFETY_C)
    named = {}  # path -> set of function names
    for r in rules:
        for fn in filter(None, (r["guard"], r["action"])):
            if fn not in safety_spans:
                errors.append(f"{r['id']}: {fn}() is not defined in safety.c")
            named.setdefault(SAFETY_C, set()).add(fn)
    for s in signals:
        path = COMPONENTS / s["file"]
        if not path.exists():
            errors.append(f"{s['id']}: {s['file']} does not exist")
            continue
        if s["function"] not in function_spans(path):
            errors.append(f"{s['id']}: {s['function']}() is not defined in {s['file']}")
        named.setdefault(path, set()).add(s["function"])
    named.setdefault(SAFETY_C, set()).update({"evaluate", "transition", "on_enter", "on_exit"})

    if not DOC.exists() or DOC.read_text() != text:
        errors.append(f"{DOC.relative_to(ROOT)} is out of date: run python3 tools/safety_rules.py gen")
    if not JS.exists() or JS.read_text() != js:
        errors.append(f"{JS.relative_to(ROOT)} is out of date: run python3 tools/safety_rules.py gen")
    if not PARAMS_JSON.exists() or PARAMS_JSON.read_text() != django:
        errors.append(f"{PARAMS_JSON.relative_to(ROOT)} is out of date: run python3 tools/safety_rules.py gen")
    if SCHEMA.read_text() != schema:
        errors.append(f"the params keys in {SCHEMA.relative_to(ROOT)} are out of date: run python3 tools/safety_rules.py gen")
    for name in uncovered:
        errors.append(f"{{{name}}} in the rules can't be changed from the dashboard: add it to rb_params.def "
                      f"(or to a dashboard card and its LIVE entry in safety_logic.js)")

    base = os.environ.get("SAFETY_RULES_BASE", "origin/main")
    if git_ok(base):
        if not changed_lines(base, DEF):
            for path, fns in named.items():
                spans = function_spans(path)
                touched = changed_lines(base, path)
                for fn in sorted(fns):
                    a, b = spans.get(fn, (0, -1))
                    if touched & set(range(a, b + 1)):
                        errors.append(f"{fn}() in {path.relative_to(COMPONENTS)} changed since {base} but "
                                      f"safety_rules.def did not: check the rules that name it")
    else:
        print(f"note: {base} not found, skipped the branch comparison")

    if errors:
        print("Safety rules check failed:")
        print("\n".join(f"  - {e}" for e in errors))
        return 1
    print(f"Safety rules OK: {len(rules)} transitions, {len(signals)} signals, doc and dashboard copy up to date")
    return 0


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "check"
    try:
        if cmd == "gen":
            text, _, _, _, js, django, schema, uncovered = generate()
            DOC.write_text(text)
            JS.write_text(js)
            PARAMS_JSON.write_text(django)
            SCHEMA.write_text(schema)
            print(f"wrote {DOC.relative_to(ROOT)}, {JS.relative_to(ROOT)}, {PARAMS_JSON.relative_to(ROOT)} and "
                  f"the params keys of {SCHEMA.relative_to(ROOT)}")
            for name in uncovered:
                print(f"warning: {{{name}}} can't be changed from the dashboard (check will fail)")
            return 0
        if cmd == "check":
            return check()
    except Error as e:
        print(f"error: {e}")
        return 1
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
