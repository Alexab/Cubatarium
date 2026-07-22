#!/usr/bin/env python3
"""Record flight_sim analyze JSON into docs/streaming/phase_runs.jsonl."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNS = ROOT / "docs" / "streaming" / "phase_runs.jsonl"


def git_rev() -> str:
    try:
        return (
            subprocess.check_output(
                ["git", "rev-parse", "--short", "HEAD"],
                cwd=ROOT,
                text=True,
            )
            .strip()
        )
    except Exception:
        return "unknown"


def git_branch() -> str:
    try:
        return (
            subprocess.check_output(
                ["git", "branch", "--show-current"],
                cwd=ROOT,
                text=True,
            )
            .strip()
        )
    except Exception:
        return "unknown"


def f2_gate(report: dict) -> bool:
    m = report.get("metrics") or {}
    checks = [
        (m.get("post_stop_black_sticky_max") or 99) <= 0,
        (m.get("post_stop_pending_med") or 99) <= 5,
        (m.get("post_stop_not_ready_end") or 99) <= 36,
        (m.get("post_stop_focus_dirty_end") or 999) <= 280,
        (m.get("stop_wall_med") or 999) <= 90,
        (m.get("chunks_traveled") or 0) >= 3,
    ]
    return all(checks)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--phase", required=True)
    ap.add_argument("--report", type=Path, required=True)
    ap.add_argument("--note", default="")
    args = ap.parse_args()

    if not args.report.is_file():
        print(f"FAIL: missing {args.report}", file=sys.stderr)
        return 1

    data = json.loads(args.report.read_text(encoding="utf-8"))
    m = data.get("metrics") or {}
    row = {
        "ts": datetime.now(timezone.utc).isoformat(),
        "phase": args.phase,
        "branch": git_branch(),
        "commit": git_rev(),
        "report": str(args.report.relative_to(ROOT))
        if args.report.is_relative_to(ROOT)
        else str(args.report),
        "pass": data.get("pass"),
        "f2_gate": f2_gate(data),
        "sticky": m.get("post_stop_black_sticky_max"),
        "nr_end": m.get("post_stop_not_ready_end"),
        "nr_delta": m.get("stop_not_ready_delta"),
        "fd_end": m.get("post_stop_focus_dirty_end"),
        "fd_delta": m.get("stop_focus_dirty_delta"),
        "holes_rate": m.get("holes_rate"),
        "wall_med": m.get("wall_ms_med"),
        "wall_fly": m.get("wall_ms_fly_med"),
        "pending_med": m.get("pending_light_focus_med"),
        "async_stuck": m.get("mesh_async_stuck_sec"),
        "cold_relight_sec": m.get("cold_relight_holes_sec"),
        "ahead": m.get("post_stop_unfinished_ahead_med"),
        "note": args.note,
    }
    RUNS.parent.mkdir(parents=True, exist_ok=True)
    with RUNS.open("a", encoding="utf-8") as f:
        f.write(json.dumps(row, ensure_ascii=False) + "\n")
    print(json.dumps(row, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
