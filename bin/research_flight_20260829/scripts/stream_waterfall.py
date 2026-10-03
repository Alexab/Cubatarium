#!/usr/bin/env python3
"""Stream waterfall: prep/emerge/relight/residual share vs baseline."""
from __future__ import annotations

import json
import statistics as st
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

FIELDS = [
    "stream_ms",
    "prep_refresh_pressure_ms",
    "mesh_emerge_ms",
    "relight_drain_ms",
    "mesh_emerge_prep_other_ms",
    "prep_refresh_gap_ms",
]


def load_spikes(path: Path) -> list[dict]:
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        if row.get("kind") == "spike":
            rows.append(row)
    return rows


def med(rows: list[dict], key: str) -> float | None:
    vals = [float(r.get(key) or 0) for r in rows if r.get(key) is not None]
    return st.median(vals) if vals else None


def waterfall(rows: list[dict]) -> dict[str, float | None]:
    stream = med(rows, "stream_ms") or 0.0
    prep = med(rows, "prep_refresh_pressure_ms")
    emerge = med(rows, "mesh_emerge_ms")
    relight = med(rows, "relight_drain_ms")
    other = None
    if stream > 0:
        tagged = sum(v or 0.0 for v in (prep, emerge, relight))
        other = max(0.0, stream - tagged)
    out: dict[str, float | None] = {
        "stream_ms": stream,
        "prep_ms": prep,
        "emerge_ms": emerge,
        "relight_ms": relight,
        "residual_ms": other,
    }
    if stream > 0:
        for k, v in list(out.items()):
            if k.endswith("_ms") and k != "stream_ms" and v is not None:
                out[k.replace("_ms", "_pct")] = 100.0 * v / stream
    return out


def top_deltas(base: list[dict], cand: list[dict], n: int = 5) -> list[tuple]:
    keys = set(FIELDS)
    for row in base + cand:
        keys.update(row.keys())
    deltas = []
    for key in sorted(keys):
        if not key.endswith("_ms") and key not in FIELDS:
            continue
        b = med(base, key)
        c = med(cand, key)
        if b is None or c is None:
            continue
        deltas.append((abs(c - b), key, b, c))
    deltas.sort(reverse=True)
    return deltas[:n]


def print_table(label: str, wf: dict[str, float | None]) -> None:
    print(f"\n== {label} ==")
    print(f"stream_ms med: {wf.get('stream_ms')}")
    for part in ("prep", "emerge", "relight", "residual"):
        ms = wf.get(f"{part}_ms")
        pct = wf.get(f"{part}_pct")
        if ms is not None:
            print(f"  {part}: {ms:.2f} ms ({pct:.1f}%)" if pct is not None else f"  {part}: {ms:.2f} ms")


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: stream_waterfall.py baseline.jsonl candidate.jsonl", file=sys.stderr)
        return 2
    base_path = Path(sys.argv[1])
    cand_path = Path(sys.argv[2])
    base = load_spikes(base_path)
    cand = load_spikes(cand_path)
    if not base or not cand:
        print("FAIL: need spike rows in both logs", file=sys.stderr)
        return 1
    print_table("baseline", waterfall(base))
    print_table("candidate", waterfall(cand))
    print("\n== top spike field deltas (med) ==")
    for _, key, b, c in top_deltas(base, cand):
        print(f"  {key}: {b:.2f} -> {c:.2f} ({c - b:+.2f})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
