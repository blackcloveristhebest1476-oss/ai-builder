#!/usr/bin/env python3
"""Turns the compile log written by log_compile.py into GitHub error annotations."""
import os
import re
import sys

path = os.environ.get("FWA_CI_LOG", "")
if not path or not os.path.exists(path):
    print("::error::No compile log found (the failure happened outside compilation).")
    sys.exit(0)

text = open(path, encoding="utf-8", errors="replace").read()
lines = text.splitlines()
pat = re.compile(r"^(?P<file>(?:[A-Za-z]:)?[^:(]+?)(?::(?P<l1>\d+):(?P<c1>\d+)|\((?P<l2>\d+),(?P<c2>\d+)\)):\s*(?P<kind>fatal error|error):\s*(?P<msg>.*)$")
loose = re.compile(r"(?:^|\s)(?:fatal error|error)\s*:\s*(?P<msg>.+)$")
errors = []
for i, line in enumerate(lines):
    m = pat.match(line.strip())
    if not m:
        lm = loose.search(line)
        if lm or "undefined reference" in line or "undefined symbol" in line or "unresolved external" in line:
            errors.append((None, None, line.strip()[:400], i))
        continue
    if m:
        f = m.group("file")
        ln = m.group("l1") or m.group("l2")
        errors.append((f, ln, m.group("msg"), i))

def clean(f):
    if not f:
        return None
    f = f.replace("\\", "/")
    idx = f.find("/src/")
    return f[idx + 1:] if idx >= 0 else f

seen = set()
count = 0
for f, ln, msg, i in errors:
    key = (clean(f), ln, msg)
    if key in seen:
        continue
    seen.add(key)
    context = " | ".join(l.strip() for l in lines[i + 1:i + 4] if l.strip())[:600]
    body = (msg + " || " + context).replace("%", "%25").replace("\r", "").replace("\n", "%0A")
    cf = clean(f)
    if cf and ln:
        print(f"::error file={cf},line={ln}::{body}")
    else:
        print(f"::error::{body}")
    count += 1
    if count >= 45:
        break
if count == 0:
    tail = " | ".join(lines[-25:])[:3000].replace("%", "%25")
    print(f"::error::No compiler error lines matched. Log tail: {tail}")
