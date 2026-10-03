from datetime import datetime
from pathlib import Path

p = Path("bin/logs")
files = sorted(p.glob("perf_*.jsonl"), key=lambda x: x.stat().st_mtime, reverse=True)[
    :12
]
for f in files:
    ts = datetime.fromtimestamp(f.stat().st_mtime)
    print(f"{f.name}\t{f.stat().st_size}\t{ts}")
