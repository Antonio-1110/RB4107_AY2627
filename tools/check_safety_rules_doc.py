#!/usr/bin/env python3
"""Check docs/safety_rules.md against the code.

- Every row of the settings table must match the Kconfig default.
- Every RB_SAFETY_* config or choice in the Kconfig must be in that table
  (options inside a choice are covered by the choice).
- Every row of the constants table must match its #define in safety.c.

It only checks numbers and names. Whether the rules themselves still match the
code is checked by reading it (see CLAUDE.md).

Usage: python3 tools/check_safety_rules_doc.py   (exit 1 on any mismatch)
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOC = ROOT / "docs" / "safety_rules.md"
KCONFIG = ROOT / "firmware" / "components" / "rb_config" / "Kconfig"
SAFETY_C = ROOT / "firmware" / "components" / "safety" / "src" / "safety.c"

ROW = re.compile(r"^\|\s*`(\w+)`\s*\|\s*([^|]+?)\s*\|")


def doc_table(text, name):
    m = re.search(rf"<!-- {name}:begin -->(.*?)<!-- {name}:end -->", text, re.S)
    if not m:
        sys.exit(f"{DOC.name}: no '{name}' table markers")
    return {r.group(1): r.group(2) for r in map(ROW.match, m.group(1).splitlines()) if r}


def kconfig_defaults(text):
    """{symbol: (kind, default)} for every config and choice, plus the set of choice members."""
    out, members, in_choice = {}, set(), False
    blocks = re.split(r"^\s*(config|choice|endchoice)\b[ \t]*(\w*)", text, flags=re.M)
    # blocks: [pre, kw, sym, body, kw, sym, body, ...]
    for kw, sym, body in zip(blocks[1::3], blocks[2::3], blocks[3::3]):
        if kw == "endchoice":
            in_choice = False
            continue
        if kw == "config" and in_choice:
            members.add(sym)
        if kw == "choice":
            in_choice = True
        d = re.search(r"^\s*default\s+(\S+)", body, re.M)
        out[sym] = (kw, d.group(1) if d else None)
    return out, members


def number(s):
    return float(re.sub(r"[uUfF]+$", "", s.strip()))


def main():
    doc = DOC.read_text()
    errors = []

    settings = doc_table(doc, "settings")
    kdefs, members = kconfig_defaults(KCONFIG.read_text())
    for sym, val in settings.items():
        if sym not in kdefs:
            errors.append(f"{sym}: in the doc, not in the Kconfig")
        elif kdefs[sym][1] != val:
            errors.append(f"{sym}: doc says {val}, Kconfig default is {kdefs[sym][1]}")
    for sym in kdefs:
        if sym.startswith("RB_SAFETY_") and sym not in members and sym not in settings:
            errors.append(f"{sym}: in the Kconfig, missing from the doc's settings table")

    constants = doc_table(doc, "constants")
    defines = dict(re.findall(r"^#define\s+(\w+)\s+(\S+)", SAFETY_C.read_text(), re.M))
    for sym, val in constants.items():
        if sym not in defines:
            errors.append(f"{sym}: in the doc, not #defined in safety.c")
        elif number(defines[sym]) != number(val):
            errors.append(f"{sym}: doc says {val}, safety.c has {defines[sym]}")
    for sym in defines:
        if sym not in constants:
            errors.append(f"{sym}: #defined in safety.c, missing from the doc's constants table")

    if errors:
        print(f"{DOC.relative_to(ROOT)} is out of date:")
        print("\n".join(f"  - {e}" for e in errors))
        return 1
    print(f"{DOC.relative_to(ROOT)}: {len(settings)} settings and {len(constants)} constants match the code")
    return 0


if __name__ == "__main__":
    sys.exit(main())
