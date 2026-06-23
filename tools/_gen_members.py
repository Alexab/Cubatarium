#!/usr/bin/env python3
import re
from pathlib import Path
from collections import Counter

src = Path(__file__).resolve().parent.parent / "src"
members = Counter()
for fp in src.rglob("*.h"):
    if "ThirdParty" in str(fp):
        continue
    for m in re.finditer(r"\b([a-z][a-zA-Z0-9]*_)\b", fp.read_text(encoding="utf-8", errors="ignore")):
        members[m.group(1)] += 1

for name in sorted(members, key=lambda x: (-len(x), x)):
    new = name[:-1]
    if new:
        new = new[0].upper() + new[1:]
    print(f'    ("{name}", "{new}"),')
