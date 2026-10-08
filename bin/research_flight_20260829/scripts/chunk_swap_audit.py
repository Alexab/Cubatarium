#!/usr/bin/env python3
"""Correlate mesh_emerge spikes with capture retarget / chunk_not_ready / churn."""
from __future__ import annotations

import json
import statistics as st
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def load_spikes(path: Path) -> list[dict]:
    rows: list[dict] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        if row.get("kind") == "spike":
            rows.append(row)
    return rows


def med(rows: list[dict], key: str) -> float | None:
    vals = [float(r[key]) for r in rows if r.get(key) is not None]
    return round(st.median(vals), 3) if vals else None


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: chunk_swap_audit.py <perf.jsonl> [baseline.jsonl]")
        return 2
    path = Path(sys.argv[1])
    if not path.is_absolute():
        path = ROOT / path
    spikes = load_spikes(path)
    if not spikes:
        print(f"no spikes in {path}")
        return 1

    emerge = [float(r.get("mesh_emerge_ms") or 0) for r in spikes]
    emerge_med = st.median(emerge) if emerge else 0.0
    emerge_p95 = sorted(emerge)[int(0.95 * (len(emerge) - 1))] if emerge else 0.0
    threshold = max(emerge_med * 1.5, emerge_p95 * 0.85, 22.0)
    hot = [r for r in spikes if float(r.get("mesh_emerge_ms") or 0) >= threshold]

    print(f"--- chunk_swap_audit: {path.name} spikes={len(spikes)} ---")
    print(f"mesh_emerge med={emerge_med:.1f} p95={emerge_p95:.1f} hot>={threshold:.1f}: {len(hot)}")

    def bucket(label: str, rows: list[dict]) -> None:
        if not rows:
            return
        print(
            f"\n{label} n={len(rows)}"
            f" retarget_med={med(rows, 'softdefer_capture_retarget_n')}"
            f" not_ready_med={med(rows, 'chunk_not_ready')}"
            f" unlit_med={med(rows, 'chunk_meshed_unlit')}"
            f" schedule_ok_med={med(rows, 'mesh_dirty_schedule_ok_n')}"
        )
        retarget_n = sum(
            1 for r in rows if float(r.get("softdefer_capture_retarget_n") or 0) > 0
        )
        not_ready_n = sum(
            1 for r in rows if float(r.get("chunk_not_ready") or 0) >= 4
        )
        print(
            f"  retarget_share={100.0 * retarget_n / len(rows):.0f}%"
            f" not_ready_ge4_share={100.0 * not_ready_n / len(rows):.0f}%"
        )

    bucket("all_spikes", spikes)
    bucket("emerge_hot", hot)
    bucket("emerge_cool", [r for r in spikes if r not in hot])

    if len(sys.argv) >= 3:
        base_path = Path(sys.argv[2])
        if not base_path.is_absolute():
            base_path = ROOT / base_path
        base_spikes = load_spikes(base_path)
        print(f"\n--- delta vs {base_path.name} ---")
        for key in (
            "mesh_emerge_ms",
            "softdefer_capture_retarget_n",
            "chunk_not_ready",
            "mesh_dirty_schedule_ok_n",
        ):
            a = med(spikes, key)
            b = med(base_spikes, key)
            if a is not None and b is not None:
                print(f"  {key}: {b} -> {a} ({a - b:+.3f})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
